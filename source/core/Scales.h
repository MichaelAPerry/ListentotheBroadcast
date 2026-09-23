// Scales, chords and pitch helpers. No JUCE dependency.
//
// Scale steps are semitone offsets from the root. Fractional values are
// microtones (3.5 = a quarter tone between a minor and a major third). The
// built-in synth plays them exactly; MIDI output uses pitch bend or rounds.
#pragma once

#include <array>
#include <cmath>

namespace ltb
{
struct Scale
{
    const char* name;
    const char* family;
    int numSteps;
    std::array<float, 8> steps;

    bool isMicrotonal() const noexcept
    {
        for (int i = 0; i < numSteps; ++i)
            if (steps[(size_t) i] - std::floor (steps[(size_t) i]) > 1.0e-4f)
                return true;
        return false;
    }
};

inline constexpr const char* kWestern = "Western";
inline constexpr const char* kMiddleEast = "Middle Eastern";
// Fixed-pitch approximations: a maqam is also its melodic path, jins structure, modulation,
// ornamentation and regional intonation, none of which a seven-note table captures.
inline constexpr const char* kMaqam = "Arabic maqam (quarter-tone approximations)";
inline constexpr const char* kEastEurope = "Eastern European";
inline constexpr const char* kSouthAsia = "South Asian (thaat)";
inline constexpr const char* kEastAsia = "East Asian";
inline constexpr const char* kSeAsia = "Southeast Asian";

inline constexpr Scale kScales[] = {
    { "Major (Ionian)", kWestern, 7, { 0, 2, 4, 5, 7, 9, 11 } },
    { "Dorian", kWestern, 7, { 0, 2, 3, 5, 7, 9, 10 } },
    { "Phrygian", kWestern, 7, { 0, 1, 3, 5, 7, 8, 10 } },
    { "Lydian", kWestern, 7, { 0, 2, 4, 6, 7, 9, 11 } },
    { "Mixolydian", kWestern, 7, { 0, 2, 4, 5, 7, 9, 10 } },
    { "Natural Minor (Aeolian)", kWestern, 7, { 0, 2, 3, 5, 7, 8, 10 } },
    { "Locrian", kWestern, 7, { 0, 1, 3, 5, 6, 8, 10 } },
    { "Harmonic Minor", kWestern, 7, { 0, 2, 3, 5, 7, 8, 11 } },
    { "Melodic Minor", kWestern, 7, { 0, 2, 3, 5, 7, 9, 11 } },
    { "Lydian Dominant", kWestern, 7, { 0, 2, 4, 6, 7, 9, 10 } },
    { "Major Pentatonic", kWestern, 5, { 0, 2, 4, 7, 9 } },
    { "Minor Pentatonic", kWestern, 5, { 0, 3, 5, 7, 10 } },
    { "Blues", kWestern, 6, { 0, 3, 5, 6, 7, 10 } },
    { "Whole Tone", kWestern, 6, { 0, 2, 4, 6, 8, 10 } },
    { "Diminished (half-whole)", kWestern, 8, { 0, 1, 3, 4, 6, 7, 9, 10 } },

    { "Hijaz / Phrygian Dominant", kMiddleEast, 7, { 0, 1, 4, 5, 7, 8, 10 } },
    { "Double Harmonic / Hijaz Kar", kMiddleEast, 7, { 0, 1, 4, 5, 7, 8, 11 } },
    { "Nahawand", kMiddleEast, 7, { 0, 2, 3, 5, 7, 8, 11 } },
    { "Kurd", kMiddleEast, 7, { 0, 1, 3, 5, 7, 8, 10 } },
    { "Nikriz", kMiddleEast, 7, { 0, 2, 3, 6, 7, 9, 10 } },
    { "Persian", kMiddleEast, 7, { 0, 1, 4, 5, 6, 8, 11 } },
    { "Arabic (Major Locrian)", kMiddleEast, 7, { 0, 2, 4, 5, 6, 8, 10 } },

    { "Rast", kMaqam, 7, { 0, 2, 3.5f, 5, 7, 9, 10.5f } },
    { "Bayati", kMaqam, 7, { 0, 1.5f, 3, 5, 7, 8, 10 } },
    { "Saba", kMaqam, 7, { 0, 1.5f, 3, 4, 7, 8, 10 } },
    { "Sikah", kMaqam, 7, { 0, 1.5f, 3.5f, 5.5f, 7, 8.5f, 10.5f } },

    { "Hungarian Minor", kEastEurope, 7, { 0, 2, 3, 6, 7, 8, 11 } },
    { "Ukrainian Dorian", kEastEurope, 7, { 0, 2, 3, 6, 7, 9, 10 } },

    { "Bhairav", kSouthAsia, 7, { 0, 1, 4, 5, 7, 8, 11 } },
    { "Bhairavi", kSouthAsia, 7, { 0, 1, 3, 5, 7, 8, 10 } },
    { "Todi", kSouthAsia, 7, { 0, 1, 3, 6, 7, 8, 11 } },
    { "Purvi", kSouthAsia, 7, { 0, 1, 4, 6, 7, 8, 11 } },
    { "Marwa", kSouthAsia, 7, { 0, 1, 4, 6, 7, 9, 11 } },
    { "Kalyan (Yaman)", kSouthAsia, 7, { 0, 2, 4, 6, 7, 9, 11 } },
    { "Khamaj", kSouthAsia, 7, { 0, 2, 4, 5, 7, 9, 10 } },
    { "Kafi", kSouthAsia, 7, { 0, 2, 3, 5, 7, 9, 10 } },
    { "Asavari", kSouthAsia, 7, { 0, 2, 3, 5, 7, 8, 10 } },

    { "Hirajoshi", kEastAsia, 5, { 0, 2, 3, 7, 8 } },
    { "In (Miyako-bushi)", kEastAsia, 5, { 0, 1, 5, 7, 8 } },
    { "Yo", kEastAsia, 5, { 0, 2, 5, 7, 9 } },
    { "Iwato", kEastAsia, 5, { 0, 1, 5, 6, 10 } },
    { "Kumoi", kEastAsia, 5, { 0, 2, 3, 7, 9 } },
    { "Ryukyu", kEastAsia, 5, { 0, 4, 5, 7, 11 } },
    { "Chinese Gong", kEastAsia, 5, { 0, 2, 4, 7, 9 } },
    { "Chinese Shang", kEastAsia, 5, { 0, 2, 5, 7, 10 } },
    { "Chinese Jue", kEastAsia, 5, { 0, 3, 5, 8, 10 } },
    { "Chinese Yu", kEastAsia, 5, { 0, 3, 5, 7, 10 } },

    { "Pelog (Selisir, approx.)", kSeAsia, 5, { 0, 1, 3, 7, 8 } },
    { "Slendro (5-equal)", kSeAsia, 5, { 0, 2.4f, 4.8f, 7.2f, 9.6f } },
};

inline constexpr int kNumScales = (int) (sizeof (kScales) / sizeof (kScales[0]));
inline constexpr const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// Semitone offset of scale degree `index` (0-based, may exceed an octave or be negative).
inline float degreeToSemitones (const Scale& scale, int index) noexcept
{
    const int n = scale.numSteps;
    int octave = index / n;
    int pos = index % n;
    if (pos < 0)
    {
        pos += n;
        --octave;
    }
    return (float) (octave * 12) + scale.steps[(size_t) pos];
}

inline float midiToHz (float pitch) noexcept
{
    return 440.0f * std::pow (2.0f, (pitch - 69.0f) / 12.0f);
}

inline int clampMidiNote (int note) noexcept
{
    while (note < 0)
        note += 12;
    while (note > 127)
        note -= 12;
    return note;
}
} // namespace ltb
