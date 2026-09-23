// Plugin parameters: what the host saves with a project and can automate.
// IDs are part of saved projects; never rename them.
#pragma once

#include "MusicEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ltb::params
{
inline constexpr const char* kSync = "sync";
inline constexpr const char* kHostTempo = "hostTempo";
inline constexpr const char* kBpm = "bpm";
inline constexpr const char* kSwing = "swing";
inline constexpr const char* kRoot = "root";
inline constexpr const char* kScale = "scale";
inline constexpr const char* kOctave = "octave";
inline constexpr const char* kChordDegree = "chordDegree";
inline constexpr const char* kChordSize = "chordSize";
inline constexpr const char* kChordStack = "chordStack";
inline constexpr const char* kChordSpread = "chordSpread";
inline constexpr const char* kProgression = "progression";
inline constexpr const char* kChangeMode = "changeMode";
inline constexpr const char* kChangeBars = "changeBars";
inline constexpr const char* kMaxNotes = "maxNotes";
inline constexpr const char* kLevel = "level";
inline constexpr const char* kReverb = "reverb";
inline constexpr const char* kSynthOn = "synthOn";
inline constexpr const char* kMidiOut = "midiOut";
inline constexpr const char* kMicrotones = "microtones";
inline constexpr const char* kBendRange = "bendRange";
inline constexpr const char* kTrafficCC = "trafficCC";

// Per traffic row: "<kind id>_<suffix>", e.g. "mdns_sound".
inline constexpr const char* kRowSuffixes[] = { "on", "role", "sound", "ch", "oct", "rhythm", "len", "vel", "max", "chance", "note" };

juce::String rowId (int kind, const char* suffix);
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Fast, allocation-free reads of every parameter on the audio thread.
class Snapshot
{
public:
    explicit Snapshot (juce::AudioProcessorValueTreeState& state);

    EngineSettings engineSettings() const noexcept;
    bool followHost() const noexcept { return get (sync) < 0.5f; }
    bool useHostTempo() const noexcept { return get (hostTempo) > 0.5f; }
    double bpm() const noexcept { return get (bpmParam); }
    float levelDb() const noexcept { return get (level); }
    float reverbMix() const noexcept { return get (reverb); }
    bool synthOn() const noexcept { return get (synthOnParam) > 0.5f; }
    bool midiOut() const noexcept { return get (midiOutParam) > 0.5f; }
    bool bendMicrotones() const noexcept { return get (microtones) < 0.5f; }
    int bendRange() const noexcept { return (int) get (bendRangeParam); }
    int trafficCC() const noexcept { return (int) get (trafficCCParam); }

private:
    static float get (const std::atomic<float>* p) noexcept { return p->load (std::memory_order_relaxed); }

    std::atomic<float>*sync, *hostTempo, *bpmParam, *swing, *root, *scale, *octave, *chordDegree, *chordSize, *chordStack,
        *chordSpread, *progression, *changeMode, *changeBars, *maxNotes, *level, *reverb, *synthOnParam, *midiOutParam,
        *microtones, *bendRangeParam, *trafficCCParam;
    std::array<std::array<std::atomic<float>*, 11>, kNumKinds> rows {};
};
} // namespace ltb::params
