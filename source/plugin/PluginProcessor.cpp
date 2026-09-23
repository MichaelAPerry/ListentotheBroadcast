#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

using namespace ltb;

ListenProcessor::ListenProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "ListenToTheBroadcast", params::createLayout()),
      params (state)
{
    channelBend.fill (8192);
    channelCC.fill (-1);
}

ListenProcessor::~ListenProcessor()
{
    if (listener != nullptr)
        listener->stop(); // joins the network thread (< 0.3 s)
}

void ListenProcessor::startNetwork()
{
    if (listener != nullptr || ! networkEnabled)
        return;
    // Join multicast groups on every local IPv4 adapter, not just the OS default:
    // virtual adapters (WSL, Hyper-V, VPN) often win the default on Windows.
    std::vector<std::string> interfaces { "0.0.0.0" };
    for (const auto& a : juce::IPAddress::getAllAddresses (false))
    {
        const auto text = a.toString();
        if (! text.startsWith ("127.") && text != "0.0.0.0")
            interfaces.push_back (text.toStdString());
    }
    listener = std::make_unique<NetListener> (defaultPortSpecs(), interfaces,
                                              [this] (const NetEvent& e) { events.push (e); });
    listener->start();
}

void ListenProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    synth.prepare (sampleRate);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    gain.reset (sampleRate, 0.05);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.levelDb()));
    startNetwork();
}

bool ListenProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && layouts.getMainInputChannelSet().isDisabled();
}

void ListenProcessor::sendAllNotesOff (juce::MidiBuffer& midi)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
        if (channelBend[(size_t) ch] != 8192)
        {
            midi.addEvent (juce::MidiMessage::pitchWheel (ch, 8192), 0);
            channelBend[(size_t) ch] = 8192;
        }
    }
}

void ListenProcessor::writeMidi (const PendingNote& e, juce::MidiBuffer& midi)
{
    const int ch = std::clamp ((int) e.channel, 1, 16);
    const int note = clampMidiNote ((int) std::lround (e.pitch));
    if (! e.on)
    {
        midi.addEvent (juce::MidiMessage::noteOff (ch, note), e.offset);
        return;
    }
    // Microtones on single-voice roles: bend the channel. Chords round to the semitone.
    int bend = 8192;
    if (e.note.mono && params.bendMicrotones())
    {
        const float frac = e.pitch - (float) std::lround (e.pitch);
        bend = std::clamp (8192 + (int) std::lround (frac / (float) params.bendRange() * 8192.0f), 0, 16383);
    }
    if (e.note.role != kRoleHit && channelBend[(size_t) ch] != bend)
    {
        midi.addEvent (juce::MidiMessage::pitchWheel (ch, bend), e.offset);
        channelBend[(size_t) ch] = bend;
    }
    midi.addEvent (juce::MidiMessage::noteOn (ch, note, juce::jlimit (0.01f, 1.0f, e.note.velocity)), e.offset);
    uiMidiSent.fetch_add (1, std::memory_order_relaxed);
}

void ListenProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    midi.clear(); // incoming MIDI is not used

    NetEvent ev;
    while (events.pop (ev))
        engine.addEvent (ev);

    const auto settings = params.engineSettings();
    const bool midiOut = params.midiOut();

    // ---- where are we in musical time?
    double bpm = params.bpm();
    bool hostPlaying = false;
    juce::Optional<double> hostPpq;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            if (params.useHostTempo())
                if (auto hostBpm = pos->getBpm(); hostBpm && *hostBpm > 1.0)
                    bpm = *hostBpm;
            hostPlaying = pos->getIsPlaying();
            hostPpq = pos->getPpqPosition();
        }
    bpm = std::clamp (bpm, 20.0, 400.0);
    const double samplesPerQuarter = sampleRate * 60.0 / bpm;
    const bool followHost = params.followHost() && hostPlaying && hostPpq.hasValue();
    // When the host is stopped (VSTHost often is), keep playing on our own clock.
    const double ppq = followHost ? *hostPpq : freePpq;
    freePpq = ppq + numSamples / samplesPerQuarter;
    uiTempo.store (bpm);
    uiFollowingHost.store (followHost);

    // ---- generate notes
    sink.count = 0;
    if (panicRequested.exchange (false))
    {
        engine.allNotesOff (sink, 0);
        synth.allNotesOff();
        sendAllNotesOff (midi);
        sink.count = 0; // the MIDI panic above already covers these
    }
    if (lastMidiOut && ! midiOut)
        sendAllNotesOff (midi);
    lastMidiOut = midiOut;

    engine.processBlock (settings, ppq, samplesPerQuarter, numSamples, sink);

    // Chronological, note-offs before note-ons at the same sample.
    std::stable_sort (sink.items.begin(), sink.items.begin() + sink.count, [] (const PendingNote& a, const PendingNote& b) {
        return a.offset != b.offset ? a.offset < b.offset : (! a.on && b.on);
    });

    // ---- built-in synth, rendered between events for sample accuracy
    for (int k = 0; k < kNumKinds; ++k)
        if (settings.rows[(size_t) k].on)
            synth.setBrightness (settings.rows[(size_t) k].sound, 0.25f + 0.75f * engine.getActivity (k));

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    int pos = 0;
    for (int i = 0; i < sink.count; ++i)
    {
        const auto& e = sink.items[(size_t) i];
        if (e.offset > pos)
        {
            synth.render (left + pos, right + pos, e.offset - pos);
            pos = e.offset;
        }
        if (e.on)
            synth.noteOn (e.id, e.note.sound, e.pitch, e.note.velocity);
        else
            synth.noteOff (e.id);
        if (midiOut)
            writeMidi (e, midi);
    }
    if (pos < numSamples)
        synth.render (left + pos, right + pos, numSamples - pos);

    // ---- traffic intensity as a CC per channel (MIDI out), a few times per beat
    const int cc = params.trafficCC();
    if (midiOut && cc >= 0 && --ccCountdown <= 0)
    {
        ccCountdown = std::max (1, (int) (samplesPerQuarter / 4 / std::max (1, numSamples)));
        for (int k = 0; k < kNumKinds; ++k)
        {
            const auto& r = settings.rows[(size_t) k];
            if (! r.on || r.role == kRoleHit)
                continue;
            const int value = juce::jlimit (0, 127, (int) std::lround (engine.getActivity (k) * 127.0f));
            if (channelCC[(size_t) r.channel] != value)
            {
                midi.addEvent (juce::MidiMessage::controllerEvent (r.channel, cc, value), 0);
                channelCC[(size_t) r.channel] = value;
            }
        }
    }

    // ---- reverb, level, gentle limiter
    if (params.synthOn())
    {
        const float mix = params.reverbMix();
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.88f;
        rp.damping = 0.35f;
        rp.width = 1.0f;
        rp.wetLevel = 0.55f * mix;
        rp.dryLevel = 1.0f - 0.45f * mix;
        reverb.setParameters (rp);
        reverb.processStereo (left, right, numSamples);

        gain.setTargetValue (juce::Decibels::decibelsToGain (params.levelDb()));
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = gain.getNextValue();
            left[i] = std::tanh (left[i] * g);
            right[i] = std::tanh (right[i] * g);
        }
    }
    else
    {
        buffer.clear();
    }
}

juce::AudioProcessorEditor* ListenProcessor::createEditor() { return new ListenEditor (*this); }

void ListenProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ListenProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ListenProcessor(); }
