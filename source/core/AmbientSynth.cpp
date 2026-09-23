#include "AmbientSynth.h"

#include "MusicEngine.h"

#include <algorithm>
#include <cmath>

namespace ltb
{
namespace
{
constexpr double kTwoPi = 6.283185307179586;

struct Timbre
{
    float attack, decay, sustain, release, gain;
};

// attack s, decay time s, sustain level, release s (to silence), output gain
constexpr Timbre kTimbres[kNumSounds] = {
    { 0.002f, 1.6f, 0.0f, 1.2f, 0.28f }, // bell
    { 0.002f, 0.6f, 0.0f, 0.3f, 0.20f }, // pluck
    { 1.600f, 2.0f, 0.8f, 3.5f, 0.07f }, // pad
    { 0.030f, 1.5f, 0.25f, 1.8f, 0.22f }, // glass
    { 0.006f, 0.8f, 0.6f, 0.5f, 0.35f }, // bass
    { 0.0005f, 0.06f, 0.0f, 0.05f, 0.10f }, // hat
    { 0, 0, 0, 0, 0 }, // midi only
};

inline float polyBlepSaw (double phase, double dt) noexcept
{
    auto t = (float) phase;
    float v = 2.0f * t - 1.0f;
    const auto d = (float) dt;
    if (t < d)
    {
        const float x = t / d;
        v -= x + x - x * x - 1.0f;
    }
    else if (t > 1.0f - d)
    {
        const float x = (t - 1.0f) / d;
        v -= x * x + x + x + 1.0f;
    }
    return v;
}

inline double wrap (double p) noexcept { return p - std::floor (p); }
} // namespace

void AmbientSynth::prepare (double sr) noexcept
{
    sampleRate = sr > 0 ? sr : 44100.0;
    voices = {};
    brightness.fill (0.3f);
}

void AmbientSynth::setBrightness (int sound, float amount) noexcept
{
    if (sound >= 0 && sound < (int) brightness.size())
        brightness[(size_t) sound] = std::clamp (amount, 0.0f, 1.0f);
}

void AmbientSynth::noteOn (uint32_t id, int sound, float pitch, float velocity) noexcept
{
    if (sound < 0 || sound >= kSoundMidiOnly)
        return;
    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.stage == kIdle)
        {
            target = &v;
            break;
        }
    if (target == nullptr) // steal: prefer the oldest releasing voice, else the oldest
        for (auto& v : voices)
            if (target == nullptr || (v.stage == kRelease) > (target->stage == kRelease)
                || ((v.stage == kRelease) == (target->stage == kRelease) && v.age < target->age))
                target = &v;

    const auto& t = kTimbres[sound];
    Voice v;
    v.id = id;
    v.age = ++counter;
    v.sound = sound;
    v.stage = kAttack;
    v.env = 0.0f;
    v.velocity = std::clamp (velocity, 0.0f, 1.0f);
    v.freq = 440.0f * std::pow (2.0f, (pitch - 69.0f) / 12.0f);
    const float pan = 0.5f + 0.35f * std::sin ((float) (id * 2654435761u % 1000u) * 0.0062832f);
    v.panL = std::cos (pan * 1.5707963f);
    v.panR = std::sin (pan * 1.5707963f);
    v.phase[1] = 0.33;
    v.phase[2] = 0.66;
    v.attackRate = 1.0f / std::max (1.0f, t.attack * (float) sampleRate);
    v.decayRate = std::exp (-1.0f / std::max (1.0f, t.decay / 3.0f * (float) sampleRate));
    v.sustain = t.sustain;
    v.releaseRate = std::exp (-1.0f / std::max (1.0f, t.release / 6.9f * (float) sampleRate));
    *target = v;
}

void AmbientSynth::startRelease (Voice& v, float seconds) noexcept
{
    v.stage = kRelease;
    v.releaseRate = std::exp (-1.0f / std::max (1.0f, seconds / 6.9f * (float) sampleRate));
}

void AmbientSynth::noteOff (uint32_t id) noexcept
{
    for (auto& v : voices)
        if (v.id == id && v.stage != kIdle && v.stage != kRelease)
            startRelease (v, kTimbres[v.sound].release);
}

void AmbientSynth::allNotesOff() noexcept
{
    for (auto& v : voices)
        if (v.stage != kIdle)
            startRelease (v, 0.05f);
}

int AmbientSynth::getActiveVoiceCount() const noexcept
{
    return (int) std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.stage != kIdle; });
}

float AmbientSynth::renderVoice (Voice& v) noexcept
{
    switch (v.stage)
    {
        case kAttack:
            v.env += v.attackRate;
            if (v.env >= 1.0f)
            {
                v.env = 1.0f;
                v.stage = kDecay;
            }
            break;
        case kDecay:
            v.env = v.sustain + (v.env - v.sustain) * v.decayRate;
            if (v.sustain <= 0.0f && v.env < 1.0e-4f)
                v.stage = kIdle;
            break;
        case kSustain:
            break;
        case kRelease:
            v.env *= v.releaseRate;
            if (v.env < 1.0e-4f)
                v.stage = kIdle;
            break;
        case kIdle:
            return 0.0f;
    }

    const double sr = sampleRate;
    const double dt = std::min (0.45, (double) v.freq / sr);
    const float bright = brightness[(size_t) v.sound];
    v.time += 1.0f / (float) sr;
    float out = 0.0f;

    switch (v.sound)
    {
        case kSoundBell:
        {
            // Two-operator FM with an inharmonic ratio; the modulation fades faster than the tone.
            const float index = 2.4f * v.env * v.env * (0.6f + bright);
            v.modPhase = wrap (v.modPhase + dt * 3.5);
            const double mod = index * std::sin (kTwoPi * v.modPhase);
            v.phase[0] = wrap (v.phase[0] + dt);
            out = (float) std::sin (kTwoPi * v.phase[0] + mod);
            break;
        }
        case kSoundPluck:
        {
            v.phase[0] = wrap (v.phase[0] + dt);
            const float saw = polyBlepSaw (v.phase[0], dt);
            const float cutoff = std::min (0.45f * (float) sr, v.freq * (1.5f + 14.0f * v.env * (0.5f + bright)));
            const float a = 1.0f - std::exp (-(float) kTwoPi * cutoff / (float) sr);
            v.lp1 += a * (saw - v.lp1);
            v.lp2 += a * (v.lp1 - v.lp2);
            out = v.lp2;
            break;
        }
        case kSoundPad:
        {
            static constexpr double detune[3] = { 1.0, 1.0041, 0.9959 };
            float sum = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                const double d = std::min (0.45, dt * detune[i]);
                v.phase[i] = wrap (v.phase[i] + d);
                sum += polyBlepSaw (v.phase[i], d);
            }
            const float cutoff = std::min (0.45f * (float) sr, 400.0f + 2600.0f * bright);
            const float a = 1.0f - std::exp (-(float) kTwoPi * cutoff / (float) sr);
            v.lp1 += a * (sum - v.lp1);
            v.lp2 += a * (v.lp1 - v.lp2);
            out = v.lp2;
            break;
        }
        case kSoundGlass:
        {
            v.phase[0] = wrap (v.phase[0] + dt);
            v.phase[1] = wrap (v.phase[1] + dt * 2.003);
            v.phase[2] = wrap (v.phase[2] + dt * 3.01);
            out = (float) (std::sin (kTwoPi * v.phase[0]) + 0.35 * std::sin (kTwoPi * v.phase[1])
                           + 0.12 * std::sin (kTwoPi * v.phase[2]));
            break;
        }
        case kSoundBass:
        {
            v.phase[0] = wrap (v.phase[0] + dt);
            const float x = (float) (std::sin (kTwoPi * v.phase[0]) + 0.25 * std::sin (2.0 * kTwoPi * v.phase[0]));
            out = std::tanh (1.6f * x);
            break;
        }
        case kSoundHit:
        {
            noiseSeed = noiseSeed * 1664525u + 1013904223u;
            const float noise = (float) (noiseSeed >> 8) / 8388608.0f - 1.0f;
            const float a = 1.0f - std::exp (-(float) kTwoPi * 6500.0f / (float) sr);
            v.lp1 += a * (noise - v.lp1);
            out = noise - v.lp1; // high-passed noise
            break;
        }
        default:
            return 0.0f;
    }
    return out * v.env * v.velocity * kTimbres[v.sound].gain;
}

void AmbientSynth::render (float* left, float* right, int numSamples) noexcept
{
    for (auto& v : voices)
    {
        if (v.stage == kIdle)
            continue;
        for (int i = 0; i < numSamples && v.stage != kIdle; ++i)
        {
            const float s = renderVoice (v);
            left[i] += s * v.panL;
            right[i] += s * v.panR;
        }
    }
}
} // namespace ltb
