// Drives the real plugin processor offline: fake traffic in, audio + MIDI out.
// Usage: ltb_plugin_tests [screenshot.png]
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

using namespace ltb;

static int failures = 0;
#define CHECK(cond)                                                                    \
    do                                                                                 \
    {                                                                                  \
        if (! (cond))                                                                  \
        {                                                                              \
            std::printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

struct FakePlayHead : juce::AudioPlayHead
{
    bool playing = false;
    double bpm = 100.0, ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        return info;
    }
};

struct Render
{
    int noteOns = 0, noteOffs = 0, allNotesOff = 0;
    std::set<int> channels;
    double sumSquares = 0.0;
    long samples = 0;
    bool finite = true;
    std::vector<double> noteOnPpq;
    float rms() const { return samples ? (float) std::sqrt (sumSquares / samples) : 0.0f; }
};

static Render run (ListenProcessor& p, double seconds, int block, FakePlayHead* head = nullptr,
                   std::function<void (int)> perBlock = {})
{
    Render out;
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    const int blocks = (int) (seconds * 48000.0 / block);
    for (int b = 0; b < blocks; ++b)
    {
        if (perBlock)
            perBlock (b);
        p.processBlock (buffer, midi);
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOn())
            {
                ++out.noteOns;
                out.channels.insert (msg.getChannel());
                if (head)
                    out.noteOnPpq.push_back (head->ppq + m.samplePosition / (48000.0 * 60.0 / head->bpm));
            }
            else if (msg.isNoteOff())
                ++out.noteOffs;
            else if (msg.isAllNotesOff())
                ++out.allNotesOff;
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float v = buffer.getSample (ch, i);
                out.finite = out.finite && std::isfinite (v);
                out.sumSquares += (double) v * v;
                ++out.samples;
            }
        if (head && head->playing)
            head->ppq += block / (48000.0 * 60.0 / head->bpm);
    }
    return out;
}

static NetEvent ev (int kind, uint32_t device)
{
    NetEvent e;
    e.kind = (uint8_t) kind;
    e.device = device;
    e.size = 200;
    return e;
}

static void setParam (ListenProcessor& p, const juce::String& id, float plainValue)
{
    auto* param = p.state.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    {
        std::printf ("free-run (no host playhead): traffic makes sound and MIDI on every row's channel\n");
        ListenProcessor p;
        p.disableNetworkForTesting();
        p.setPlayConfigDetails (0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        auto r = run (p, 12.0, 512, nullptr, [&p] (int b) {
            if (b % 20 == 0)
                for (int k = 0; k < kNumKinds; ++k)
                    p.injectEvent (ev (k, (uint32_t) (b * 7 + k)));
        });
        std::printf ("  note-ons %d, note-offs %d, channels %zu, rms %.4f\n", r.noteOns, r.noteOffs, r.channels.size(), r.rms());
        CHECK (r.finite);
        CHECK (r.rms() > 0.005f && r.rms() < 0.9f);
        CHECK (r.noteOns > 20);
        CHECK (r.channels == (std::set<int> { 1, 2, 3, 4, 5, 6, 10 }));

        // Everything stops: after traffic ends, all notes finish and the output decays.
        auto tail = run (p, 30.0, 512);
        CHECK (p.engine.getActiveNoteCount() == 0);
        auto silent = run (p, 2.0, 512);
        std::printf ("  after traffic stops: rms %.6f\n", silent.rms());
        CHECK (silent.rms() < 1.0e-3f);
    }

    {
        std::printf ("follows the host transport: notes land on the host's grid\n");
        ListenProcessor p;
        p.disableNetworkForTesting();
        FakePlayHead head;
        head.playing = true;
        head.bpm = 100.0;
        head.ppq = 16.37;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 441);
        for (int k = 0; k < kNumKinds; ++k)
            if (k != kMdns)
                setParam (p, params::rowId (k, "on"), 0.0f);
        setParam (p, params::rowId (kMdns, "rhythm"), 2.0f); // quarter notes
        setParam (p, params::rowId (kMdns, "chance"), 1.0f);
        auto r = run (p, 6.0, 441, &head, [&p] (int b) {
            if (b % 13 == 0)
                p.injectEvent (ev (kMdns, 5));
        });
        bool allOnBeats = ! r.noteOnPpq.empty();
        for (double t : r.noteOnPpq)
            allOnBeats = allOnBeats && std::abs (t - std::round (t)) < 0.001;
        std::printf ("  %zu notes, all on host beats: %s\n", r.noteOnPpq.size(), allOnBeats ? "yes" : "no");
        CHECK (allOnBeats);
        CHECK (std::abs (p.getCurrentTempo() - 100.0) < 1e-9 && p.isFollowingHost());

        std::printf ("host stopped: keeps playing on its own clock\n");
        head.playing = false;
        auto stopped = run (p, 4.0, 441, &head, [&p] (int b) {
            if (b % 13 == 0)
                p.injectEvent (ev (kMdns, 5));
        });
        CHECK (stopped.noteOns > 0 && ! p.isFollowingHost());
        p.setPlayHead (nullptr);
    }

    {
        std::printf ("panic, test button, MIDI-out toggle, built-in sound toggle\n");
        ListenProcessor p;
        p.disableNetworkForTesting();
        p.prepareToPlay (48000.0, 256);
        p.requestTest (kSsdp);
        auto t = run (p, 0.1, 256);
        CHECK (t.noteOns == 3); // a pad chord, immediately, no traffic needed
        p.requestPanic();
        auto after = run (p, 0.2, 256);
        CHECK (after.allNotesOff == 16);
        CHECK (p.engine.getActiveNoteCount() == 0);

        setParam (p, params::kMidiOut, 0.0f);
        p.requestTest (kMdns);
        auto noMidi = run (p, 0.5, 256);
        CHECK (noMidi.noteOns == 0 && noMidi.rms() > 0.0f);

        setParam (p, params::kSynthOn, 0.0f);
        setParam (p, params::kMidiOut, 1.0f);
        p.requestTest (kMdns);
        auto midiOnly = run (p, 0.5, 256);
        CHECK (midiOnly.noteOns == 1 && midiOnly.rms() == 0.0f);
    }

    {
        std::printf ("state saves and restores (host project recall)\n");
        ListenProcessor a;
        a.disableNetworkForTesting();
        setParam (a, params::kScale, 24.0f); // Saba
        setParam (a, params::rowId (kLanSync, "on"), 0.0f);
        setParam (a, params::kBpm, 72.0f);
        juce::MemoryBlock blob;
        a.getStateInformation (blob);
        ListenProcessor b;
        b.disableNetworkForTesting();
        b.setStateInformation (blob.getData(), (int) blob.getSize());
        CHECK ((int) b.state.getRawParameterValue (params::kScale)->load() == 24);
        CHECK (b.state.getRawParameterValue (params::rowId (kLanSync, "on"))->load() < 0.5f);
        CHECK (std::abs (b.state.getRawParameterValue (params::kBpm)->load() - 72.0f) < 0.01f);
    }

    {
        std::printf ("real network listener starts with the host's audio and stops with the plugin\n");
        auto p = std::make_unique<ListenProcessor>();
        CHECK (p->getListener() == nullptr); // nothing during plugin scanning
        p->prepareToPlay (48000.0, 512);
        CHECK (p->getListener() != nullptr);
        int ok = 0;
        for (auto& st : p->getListener()->getStatus())
            ok += st.ok ? 1 : 0;
        std::printf ("  %d ports open\n", ok);
        CHECK (ok > 0);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        p.reset();
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("  plugin closed in %.0f ms\n", ms);
        CHECK (ms < 1000.0);
    }

    if (argc > 1)
    {
        std::printf ("rendering the editor to %s\n", argv[1]);
        ListenProcessor p;
        p.disableNetworkForTesting();
        p.prepareToPlay (48000.0, 512);
        run (p, 3.0, 512, nullptr, [&p] (int b) {
            if (b % 9 == 0)
                p.injectEvent (ev (b % kNumKinds, (uint32_t) b));
        });
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
        CHECK (editor != nullptr);
        if (editor != nullptr)
        {
            const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (img, stream);
        }
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nall plugin tests passed\n", failures);
    return failures ? 1 : 0;
}
