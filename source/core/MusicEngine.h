// Turns traffic events into quantized, scale-aware notes. Runs on the audio thread:
// no locks, no allocation. Time is measured in quarter notes (PPQ), so the grid
// follows the host's transport when it plays, or a free-running clock otherwise.
#pragma once

#include "NetEvent.h"
#include "Scales.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace ltb
{
enum Role : uint8_t { kRoleNote, kRoleChord, kRolePad, kRoleBass, kRoleHit, kNumRoles };
inline constexpr const char* kRoleNames[kNumRoles] = { "Note", "Chord", "Pad", "Bass", "Hit" };

enum Sound : uint8_t { kSoundBell, kSoundPluck, kSoundPad, kSoundGlass, kSoundBass, kSoundHit, kSoundMidiOnly, kNumSounds };
inline constexpr const char* kSoundNames[kNumSounds] = { "Bell", "Pluck", "Pad", "Glass", "Bass", "Hat", "MIDI only" };

inline constexpr int kDivisions[] = { 1, 2, 4, 8, 16 }; // in 16th steps
inline constexpr const char* kDivisionNames[] = { "1/16", "1/8", "1/4", "1/2", "1 bar" };
inline constexpr int kNumDivisions = 5;

struct Progression
{
    const char* name;
    int length;
    std::array<int, 4> degrees;
    bool randomWalk;
};

inline constexpr Progression kProgressions[] = {
    { "Hold", 1, { 0 }, false },
    { "Drift (I-IV-I-V)", 4, { 0, 3, 0, 4 }, false },
    { "Pop (I-V-vi-IV)", 4, { 0, 4, 5, 3 }, false },
    { "Rise (I-ii-iii-IV)", 4, { 0, 1, 2, 3 }, false },
    { "Pendulum (I-II)", 2, { 0, 1 }, false },
    { "Andalusian (i-VII-VI-V)", 4, { 0, 6, 5, 4 }, false },
    { "Random walk", 1, { 0 }, true },
};
inline constexpr int kNumProgressions = (int) (sizeof (kProgressions) / sizeof (kProgressions[0]));

struct RowSettings
{
    bool on = true;
    int role = kRoleNote;
    int sound = kSoundBell;
    int channel = 1; // 1..16, MIDI output only
    int octave = 0;
    int division = 1; // index into kDivisions
    int length = 4; // in 16th steps
    int velocity = 90; // 1..127
    int density = 8; // max triggers per bar
    float chance = 1.0f;
    int hitNote = 60;
};

struct EngineSettings
{
    float swing = 0.0f; // 0..1: odd 16ths delayed by up to a 32nd
    int root = 2; // D
    int scale = 1; // Dorian
    int baseOctave = 4;
    int chordDegree = 0;
    int chordSize = 3;
    int chordStack = 2; // scale steps between chord tones (2 = thirds)
    int chordSpread = 0;
    int progression = 1;
    bool changeOnNewDevice = false;
    int changeBars = 4;
    float level = 1.0f;
    int maxNotesPerStep = 6;
    bool deviceMotifs = true; // note role: each device walks its own short phrase instead of one note
    std::array<RowSettings, kNumKinds> rows;

    static EngineSettings defaults();
};

struct NoteOn
{
    uint32_t id;
    uint8_t kind, role, sound, channel; // channel 1..16
    float pitch; // fractional MIDI pitch
    float velocity; // 0..1
    bool mono; // single-voice role: MIDI output may pitch-bend the channel for microtones
};

class NoteSink
{
public:
    virtual ~NoteSink() = default;
    virtual void noteOn (const NoteOn& note, int sampleOffset) = 0;
    virtual void noteOff (uint32_t id, uint8_t channel, float pitch, int sampleOffset) = 0;
};

class MusicEngine
{
public:
    MusicEngine();

    void reset() noexcept;
    void addEvent (const NetEvent& ev) noexcept;
    void triggerTest (int kind) noexcept { testRequests.fetch_or (1u << kind); }

    // `ppqStart`: block start position in quarter notes; `samplesPerQuarter` from tempo and sample rate.
    void processBlock (const EngineSettings& s, double ppqStart, double samplesPerQuarter, int numSamples, NoteSink& sink) noexcept;
    void allNotesOff (NoteSink& sink, int sampleOffset) noexcept;

    // Read by the UI thread.
    int getPackets (int kind) const noexcept { return packets[(size_t) kind].load (std::memory_order_relaxed); }
    int getNotes (int kind) const noexcept { return notesPlayed[(size_t) kind].load (std::memory_order_relaxed); }
    float getActivity (int kind) const noexcept { return activity[(size_t) kind].load (std::memory_order_relaxed); }
    int getBar() const noexcept { return uiBar.load (std::memory_order_relaxed); }
    int getBeat() const noexcept { return uiBeat.load (std::memory_order_relaxed); }
    int getChordRootPitchClass() const noexcept { return uiChordPc.load (std::memory_order_relaxed); }
    int getActiveNoteCount() const noexcept { return uiActive.load (std::memory_order_relaxed); }

    int chordRootDegree (const EngineSettings& s) const noexcept;

private:
    struct Pending
    {
        int count = 0;
        uint32_t device = 0;
        uint16_t size = 0;
    };
    struct Active
    {
        uint32_t id = 0;
        uint8_t channel = 0, kind = 0;
        int midiNote = 0;
        float pitch = 0;
        int64_t offSample = 0;
        bool pad = false;
    };

    void onStep (const EngineSettings& s, int64_t step, int offset, double samplesPerStep, NoteSink& sink) noexcept;
    void updateChord (const EngineSettings& s, int64_t bar) noexcept;
    int fire (const EngineSettings& s, int kind, const Pending& p, int budget, int offset, double samplesPerStep,
              NoteSink& sink, bool pad) noexcept;
    void release (size_t index, int offset, NoteSink& sink) noexcept;
    uint32_t nextRandom() noexcept;

    std::array<Pending, kNumKinds> pending {};
    std::array<float, kNumKinds> act {};
    std::array<int, kNumKinds> firesThisBar {};
    std::array<int64_t, kNumKinds> lastTrafficStep {}; // for pads: only breathe while traffic is recent

    // How many notes each device has played, for its motif. Fixed size: the least-used slot is recycled.
    struct DeviceStep
    {
        uint32_t device = 0;
        uint32_t count = 0;
    };
    std::array<DeviceStep, 64> deviceSteps {};
    uint32_t nextMotifStep (uint32_t device) noexcept;
    std::array<Active, 256> active {};
    size_t numActive = 0;

    int64_t sampleClock = 0;
    int64_t lastStep = INT64_MIN;
    double lastPpqEnd = -1.0;
    uint32_t nextId = 1, rng = 0x9E3779B9u;

    int progressionCounter = 0; // new-device mode and random walk
    int64_t lastProgressionBar = INT64_MIN;
    bool newDeviceSeen = false;
    int lastChordRoot = INT32_MIN;
    bool chordChanged = true;

    std::atomic<uint32_t> testRequests { 0 };
    std::array<std::atomic<int>, kNumKinds> packets {}, notesPlayed {};
    std::array<std::atomic<float>, kNumKinds> activity {};
    std::atomic<int> uiBar { 1 }, uiBeat { 1 }, uiChordPc { 2 }, uiActive { 0 };
};
} // namespace ltb
