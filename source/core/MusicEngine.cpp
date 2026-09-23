#include "MusicEngine.h"

#include <algorithm>
#include <cmath>

namespace ltb
{
namespace
{
int64_t floorDiv (int64_t a, int64_t b) noexcept
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

RowSettings row (int role, int sound, int channel, int octave, int division, int length, int velocity, int density,
                 float chance, int hitNote = 60)
{
    RowSettings r;
    r.role = role;
    r.sound = sound;
    r.channel = channel;
    r.octave = octave;
    r.division = division;
    r.length = length;
    r.velocity = velocity;
    r.density = density;
    r.chance = chance;
    r.hitNote = hitNote;
    return r;
}
} // namespace

EngineSettings EngineSettings::defaults()
{
    EngineSettings s;
    //                          role        sound        ch oct div len vel dens chance
    s.rows[kMdns] = row (kRoleNote, kSoundBell, 1, 0, 1, 4, 80, 8, 0.9f);
    s.rows[kSsdp] = row (kRolePad, kSoundPad, 2, 0, 4, 64, 60, 1, 1.0f);
    s.rows[kLlmnr] = row (kRoleNote, kSoundGlass, 3, 1, 2, 2, 70, 4, 0.8f);
    s.rows[kWsd] = row (kRoleNote, kSoundPluck, 4, 1, 3, 8, 75, 2, 1.0f);
    s.rows[kDhcp] = row (kRoleChord, kSoundBell, 5, 0, 4, 32, 90, 1, 1.0f);
    s.rows[kNetbios] = row (kRoleBass, kSoundBass, 6, -1, 3, 16, 85, 2, 1.0f);
    s.rows[kLanSync] = row (kRoleHit, kSoundHit, 10, 0, 2, 1, 55, 4, 1.0f, 42);
    return s;
}

MusicEngine::MusicEngine() { reset(); }

void MusicEngine::reset() noexcept
{
    pending = {};
    act = {};
    firesThisBar = {};
    lastTrafficStep.fill (INT64_MIN / 2);
    numActive = 0;
    sampleClock = 0;
    lastStep = INT64_MIN;
    lastPpqEnd = -1.0;
    progressionCounter = 0;
    lastProgressionBar = INT64_MIN;
    newDeviceSeen = false;
    lastChordRoot = INT32_MIN;
    chordChanged = true;
}

uint32_t MusicEngine::nextRandom() noexcept
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void MusicEngine::addEvent (const NetEvent& ev) noexcept
{
    if (ev.kind >= kNumKinds)
        return;
    auto& p = pending[ev.kind];
    p.count = std::min (p.count + 1, 1000);
    p.device = ev.device;
    p.size = ev.size;
    act[ev.kind] = std::min (act[ev.kind] + 1.0f, 24.0f); // a burst keeps a pad breathing ~8 bars, not forever
    newDeviceSeen = newDeviceSeen || ev.newDevice;
    packets[ev.kind].fetch_add (1, std::memory_order_relaxed);
}

int MusicEngine::chordRootDegree (const EngineSettings& s) const noexcept
{
    const auto& prog = kProgressions[std::clamp (s.progression, 0, kNumProgressions - 1)];
    if (prog.randomWalk)
        return s.chordDegree + progressionCounter;
    const int index = ((progressionCounter % prog.length) + prog.length) % prog.length;
    return s.chordDegree + prog.degrees[(size_t) index];
}

void MusicEngine::updateChord (const EngineSettings& s, int64_t bar) noexcept
{
    static constexpr int walkSteps[] = { -2, -1, 1, 2, 3 };
    const auto& prog = kProgressions[std::clamp (s.progression, 0, kNumProgressions - 1)];
    const int changeBars = std::max (1, s.changeBars);

    if (s.changeOnNewDevice)
    {
        if (newDeviceSeen)
            progressionCounter = prog.randomWalk ? (progressionCounter + walkSteps[nextRandom() % 5] + 7) % 7
                                                 : progressionCounter + 1;
    }
    else if (prog.randomWalk)
    {
        if (bar % changeBars == 0 && bar != lastProgressionBar)
        {
            progressionCounter = (progressionCounter + walkSteps[nextRandom() % 5] + 7) % 7;
            lastProgressionBar = bar;
        }
    }
    else
    {
        // Derived from the bar number, so the progression lines up with the host's timeline.
        progressionCounter = (int) floorDiv (bar, changeBars);
    }
    newDeviceSeen = false;
}

void MusicEngine::processBlock (const EngineSettings& s, double ppqStart, double samplesPerQuarter, int numSamples,
                                NoteSink& sink) noexcept
{
    if (numSamples <= 0 || samplesPerQuarter <= 0)
        return;
    const double ppqEnd = ppqStart + numSamples / samplesPerQuarter;
    const double samplesPerStep = samplesPerQuarter / 4.0;

    // Transport jumped (loop, relocate, first block): resume the grid from here.
    if (std::abs (ppqStart - lastPpqEnd) > 1.0e-3)
        lastStep = (int64_t) std::ceil (ppqStart * 4.0 - 1.0e-9) - 1;
    lastPpqEnd = ppqEnd;

    if (const auto tests = testRequests.exchange (0))
        for (int k = 0; k < kNumKinds; ++k)
            if (tests & (1u << k))
            {
                Pending p;
                p.count = 1;
                p.device = 0x5EED0000u + (uint32_t) k;
                p.size = 100;
                fire (s, k, p, 16, 0, samplesPerStep, sink, s.rows[(size_t) k].role == kRolePad);
            }

    const double swingPpq = std::clamp (s.swing, 0.0f, 1.0f) * 0.125;
    const int64_t first = (int64_t) std::floor (ppqStart * 4.0) - 1;
    const int64_t last = (int64_t) std::ceil (ppqEnd * 4.0);
    for (int64_t k = std::max (first, lastStep + 1); k <= last; ++k)
    {
        const double t = (double) k * 0.25 + ((k & 1) ? swingPpq : 0.0);
        if (t < ppqStart - 1.0e-9 || t >= ppqEnd)
            continue;
        const int offset = std::clamp ((int) ((t - ppqStart) * samplesPerQuarter), 0, numSamples - 1);
        onStep (s, k, offset, samplesPerStep, sink);
        lastStep = k;
    }

    // Release notes that end in this block (including ones started above).
    const int64_t blockEnd = sampleClock + numSamples;
    for (size_t i = 0; i < numActive;)
    {
        if (active[i].offSample < blockEnd)
            release (i, (int) std::clamp<int64_t> (active[i].offSample - sampleClock, 0, numSamples - 1), sink);
        else
            ++i;
    }
    sampleClock = blockEnd;

    const auto barBeat = (int64_t) std::floor (ppqStart);
    uiBar.store ((int) floorDiv (barBeat, 4) + 1, std::memory_order_relaxed);
    uiBeat.store ((int) (barBeat - floorDiv (barBeat, 4) * 4) + 1, std::memory_order_relaxed);
    uiActive.store ((int) numActive, std::memory_order_relaxed);
    const auto& scale = kScales[std::clamp (s.scale, 0, kNumScales - 1)];
    const int pc = (int) std::lround (s.root + degreeToSemitones (scale, chordRootDegree (s)));
    uiChordPc.store (((pc % 12) + 12) % 12, std::memory_order_relaxed);
}

void MusicEngine::onStep (const EngineSettings& s, int64_t step, int offset, double samplesPerStep, NoteSink& sink) noexcept
{
    const int stepInBar = (int) (step - floorDiv (step, 16) * 16);
    const int64_t bar = floorDiv (step, 16);

    for (int k = 0; k < kNumKinds; ++k)
    {
        if (pending[(size_t) k].count > 0)
            lastTrafficStep[(size_t) k] = step;
        act[(size_t) k] *= 0.97f;
        activity[(size_t) k].store (1.0f - std::exp (-act[(size_t) k] / 2.0f), std::memory_order_relaxed);
    }

    if (stepInBar == 0)
    {
        firesThisBar = {};
        updateChord (s, bar);
    }
    const int root = chordRootDegree (s);
    if (root != lastChordRoot)
    {
        chordChanged = true;
        lastChordRoot = root;
    }

    int budget = std::clamp (s.maxNotesPerStep, 1, 32);
    for (int k = 0; k < kNumKinds && budget > 0; ++k)
    {
        const auto& r = s.rows[(size_t) k];
        auto& p = pending[(size_t) k];
        if (! r.on)
        {
            p = {};
            continue;
        }
        if (r.role == kRolePad)
        {
            if (stepInBar != 0)
                continue;
            bool sounding = false;
            for (size_t i = 0; i < numActive; ++i)
                sounding = sounding || (active[i].pad && active[i].kind == k);
            // Keep the pad going while this traffic was heard in the last two bars; then let it end.
            const bool busy = step - lastTrafficStep[(size_t) k] <= 32;
            const Pending snapshot = p;
            p = {};
            if (busy && (! sounding || chordChanged))
                budget -= fire (s, k, snapshot, budget, offset, samplesPerStep, sink, true);
            continue;
        }
        const int division = kDivisions[std::clamp (r.division, 0, kNumDivisions - 1)];
        if (stepInBar % division != 0 || p.count == 0)
            continue;
        const Pending snapshot = p;
        p = {};
        if (firesThisBar[(size_t) k] >= r.density || (float) (nextRandom() % 10000) / 10000.0f >= r.chance)
            continue;
        budget -= fire (s, k, snapshot, budget, offset, samplesPerStep, sink, false);
    }
    if (stepInBar == 0)
        chordChanged = false;
}

int MusicEngine::fire (const EngineSettings& s, int kind, const Pending& p, int budget, int offset,
                       double samplesPerStep, NoteSink& sink, bool pad) noexcept
{
    const auto& r = s.rows[(size_t) kind];
    const auto& scale = kScales[std::clamp (s.scale, 0, kNumScales - 1)];
    const float base = (float) (12 * (s.baseOctave + 1) + s.root + 12 * r.octave);
    const int chordRoot = chordRootDegree (s);

    std::array<float, 8> pitches {};
    int numPitches = 0;
    switch (r.role)
    {
        case kRoleHit:
            pitches[0] = (float) r.hitNote;
            numPitches = 1;
            break;
        case kRoleBass:
            pitches[0] = base - 12.0f + degreeToSemitones (scale, chordRoot);
            numPitches = 1;
            break;
        case kRoleChord:
        case kRolePad:
            numPitches = std::clamp (s.chordSize, 1, 6);
            for (int i = 0; i < numPitches; ++i)
            {
                const float spread = i > 0 ? 12.0f * (float) (i % (std::max (0, s.chordSpread) + 1)) : 0.0f;
                pitches[(size_t) i] = base + degreeToSemitones (scale, chordRoot + i * std::max (1, s.chordStack)) + spread;
            }
            break;
        default:
        {
            // Each device gets a stable scale degree relative to the current chord.
            const int motif = (int) (p.device % (uint32_t) scale.numSteps);
            pitches[0] = base + degreeToSemitones (scale, chordRoot + motif) + (p.size > 400 ? 12.0f : 0.0f);
            numPitches = 1;
        }
    }
    numPitches = std::min (numPitches, std::max (0, budget));
    if (numPitches == 0)
        return 0;

    if (pad)
        for (size_t i = 0; i < numActive;)
        {
            if (active[i].pad && active[i].kind == kind)
                release (i, offset, sink);
            else
                ++i;
        }

    float velocity = (float) r.velocity / 127.0f * s.level;
    if (p.count > 0)
        velocity *= 0.75f + 0.25f * std::min (1.0f, (float) p.count / 4.0f);
    velocity = std::clamp (velocity, 0.01f, 1.0f);

    const auto channel = (uint8_t) std::clamp (r.channel, 1, 16);
    const int64_t offSample = sampleClock + offset + (int64_t) (std::max (1, r.length) * samplesPerStep);
    for (int i = 0; i < numPitches; ++i)
    {
        const float pitch = pitches[(size_t) i];
        const int midiNote = clampMidiNote ((int) std::lround (pitch));
        // Retrigger: end the previous note on the same channel/key first.
        for (size_t j = 0; j < numActive; ++j)
            if (active[j].channel == channel && active[j].midiNote == midiNote)
            {
                release (j, offset, sink);
                break;
            }
        if (numActive == active.size())
            release (0, offset, sink); // oldest
        Active a;
        a.id = nextId++;
        a.channel = channel;
        a.kind = (uint8_t) kind;
        a.midiNote = midiNote;
        a.pitch = pitch;
        a.offSample = offSample;
        a.pad = pad;
        active[numActive++] = a;

        NoteOn n { a.id, (uint8_t) kind, (uint8_t) r.role, (uint8_t) r.sound, channel, pitch, velocity,
                   r.role == kRoleNote || r.role == kRoleBass };
        sink.noteOn (n, offset);
    }
    ++firesThisBar[(size_t) kind];
    notesPlayed[(size_t) kind].fetch_add (numPitches, std::memory_order_relaxed);
    return numPitches;
}

void MusicEngine::release (size_t index, int offset, NoteSink& sink) noexcept
{
    const auto a = active[index];
    // Keep order (oldest first) so voice stealing picks the oldest note.
    for (size_t i = index + 1; i < numActive; ++i)
        active[i - 1] = active[i];
    --numActive;
    sink.noteOff (a.id, a.channel, a.pitch, offset);
}

void MusicEngine::allNotesOff (NoteSink& sink, int offset) noexcept
{
    while (numActive > 0)
        release (numActive - 1, offset, sink);
    pending = {};
}
} // namespace ltb
