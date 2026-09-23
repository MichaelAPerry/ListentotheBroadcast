// Small polyphonic synth with a few ambient timbres. Plays fractional pitches
// exactly, so quarter-tone scales need no pitch bend. Audio thread only; no
// allocation after prepare().
#pragma once

#include <array>
#include <cstdint>

namespace ltb
{
class AmbientSynth
{
public:
    static constexpr int kMaxVoices = 48;

    void prepare (double sampleRate) noexcept;
    void noteOn (uint32_t id, int sound, float pitch, float velocity) noexcept;
    void noteOff (uint32_t id) noexcept;
    void allNotesOff() noexcept; // fast fade, no clicks
    void setBrightness (int sound, float amount01) noexcept; // e.g. from traffic intensity

    // Adds into the buffers (does not clear them).
    void render (float* left, float* right, int numSamples) noexcept;
    int getActiveVoiceCount() const noexcept;

private:
    enum Stage : uint8_t { kIdle, kAttack, kDecay, kSustain, kRelease };

    struct Voice
    {
        uint32_t id = 0;
        uint64_t age = 0;
        int sound = 0;
        Stage stage = kIdle;
        float env = 0, velocity = 0, freq = 0, panL = 0.7f, panR = 0.7f;
        double phase[3] = { 0, 0, 0 }, modPhase = 0;
        float lp1 = 0, lp2 = 0, hpState = 0, prevNoise = 0;
        float attackRate = 0, decayRate = 0, sustain = 0, releaseRate = 0;
        float time = 0;
    };

    float renderVoice (Voice& v) noexcept;
    void startRelease (Voice& v, float seconds) noexcept;

    std::array<Voice, kMaxVoices> voices {};
    std::array<float, 8> brightness {};
    double sampleRate = 44100.0;
    uint64_t counter = 0;
    uint32_t noiseSeed = 22222u;
};
} // namespace ltb
