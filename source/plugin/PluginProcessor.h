#pragma once

#include "AmbientSynth.h"
#include "MusicEngine.h"
#include "NetListener.h"
#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>

class ListenProcessor final : public juce::AudioProcessor
{
public:
    ListenProcessor();
    ~ListenProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    // Factory presets, as host programs.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Called from the editor (message thread).
    void requestTest (int kind) noexcept { engine.triggerTest (kind); }
    void requestPanic() noexcept { panicRequested.store (true); }
    void injectEvent (const ltb::NetEvent& e) noexcept { events.push (e); } // tests, and the listener

    juce::AudioProcessorValueTreeState state;
    ltb::MusicEngine engine;

    // Status for the editor.
    const ltb::NetListener* getListener() const noexcept { return listener.get(); }
    double getCurrentTempo() const noexcept { return uiTempo.load(); }
    bool isFollowingHost() const noexcept { return uiFollowingHost.load(); }
    int getMidiSent() const noexcept { return uiMidiSent.load(); }

    // For tests: turn off the real network listener before prepareToPlay.
    void disableNetworkForTesting() noexcept { networkEnabled = false; }
    // For demos: a listener that opens no sockets and is fed with simulatePacket() instead.
    void useSimulatedNetwork() noexcept { simulatedNetwork = true; }
    void simulatePacket (uint8_t kind, uint16_t port, uint32_t ipv4, const void* data, size_t size, double seconds)
    {
        if (listener != nullptr && listener->isSimulated())
            listener->simulatePacket (kind, port, ipv4, static_cast<const uint8_t*> (data), size, seconds);
    }

private:
    struct PendingNote
    {
        int offset;
        bool on;
        ltb::NoteOn note;
        uint32_t id;
        uint8_t channel;
        float pitch;
    };

    class Sink final : public ltb::NoteSink
    {
    public:
        std::array<PendingNote, 1024> items {};
        int count = 0;
        void noteOn (const ltb::NoteOn& n, int offset) override
        {
            if (count < (int) items.size())
                items[(size_t) count++] = { offset, true, n, n.id, n.channel, n.pitch };
        }
        void noteOff (uint32_t id, uint8_t channel, float pitch, int offset) override
        {
            if (count < (int) items.size())
                items[(size_t) count++] = { offset, false, {}, id, channel, pitch };
        }
    };

    void startNetwork();
    void writeMidi (const PendingNote& e, juce::MidiBuffer& midi);
    void sendAllNotesOff (juce::MidiBuffer& midi);

    ltb::params::Snapshot params;
    ltb::SpscQueue<ltb::NetEvent, 2048> events;
    std::unique_ptr<ltb::NetListener> listener;
    bool networkEnabled = true;
    bool simulatedNetwork = false;

    ltb::AmbientSynth synth;
    juce::Reverb reverb;
    juce::SmoothedValue<float> gain;
    Sink sink;

    double sampleRate = 44100.0;
    double freePpq = 0.0;
    bool lastMidiOut = true;
    std::array<int, 17> channelBend {}; // current 14-bit bend per MIDI channel (1..16), 8192 = centre
    std::array<int, 17> channelCC {};
    int ccCountdown = 0;

    std::atomic<bool> panicRequested { false };
    std::atomic<double> uiTempo { 84.0 };
    std::atomic<bool> uiFollowingHost { false };
    std::atomic<int> uiMidiSent { 0 };
    std::atomic<int> currentProgram { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListenProcessor)
};
