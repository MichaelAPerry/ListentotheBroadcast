"""Scales, chords and pitch helpers.

Scale steps are semitone offsets from the root. Fractional values are
microtones (e.g. 3.5 = a quarter-tone between a minor and major third) and are
rendered with pitch bend when the engine's microtone mode is "bend".
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Scale:
    key: str
    name: str
    family: str
    steps: tuple[float, ...]

    @property
    def microtonal(self) -> bool:
        return any(s != int(s) for s in self.steps)


_W = "Western"
_ME = "Middle Eastern (12-TET)"
_MQ = "Arabic maqam (quarter-tone)"
_EE = "Eastern European"
_SA = "South Asian (Hindustani thaat)"
_EA = "East Asian"
_SEA = "Southeast Asian"

SCALES: tuple[Scale, ...] = (
    # Western
    Scale("major", "Major (Ionian)", _W, (0, 2, 4, 5, 7, 9, 11)),
    Scale("dorian", "Dorian", _W, (0, 2, 3, 5, 7, 9, 10)),
    Scale("phrygian", "Phrygian", _W, (0, 1, 3, 5, 7, 8, 10)),
    Scale("lydian", "Lydian", _W, (0, 2, 4, 6, 7, 9, 11)),
    Scale("mixolydian", "Mixolydian", _W, (0, 2, 4, 5, 7, 9, 10)),
    Scale("minor", "Natural Minor (Aeolian)", _W, (0, 2, 3, 5, 7, 8, 10)),
    Scale("locrian", "Locrian", _W, (0, 1, 3, 5, 6, 8, 10)),
    Scale("harmonic_minor", "Harmonic Minor", _W, (0, 2, 3, 5, 7, 8, 11)),
    Scale("melodic_minor", "Melodic Minor", _W, (0, 2, 3, 5, 7, 9, 11)),
    Scale("lydian_dominant", "Lydian Dominant", _W, (0, 2, 4, 6, 7, 9, 10)),
    Scale("major_pentatonic", "Major Pentatonic", _W, (0, 2, 4, 7, 9)),
    Scale("minor_pentatonic", "Minor Pentatonic", _W, (0, 3, 5, 7, 10)),
    Scale("blues", "Blues", _W, (0, 3, 5, 6, 7, 10)),
    Scale("whole_tone", "Whole Tone", _W, (0, 2, 4, 6, 8, 10)),
    Scale("diminished", "Diminished (half-whole)", _W, (0, 1, 3, 4, 6, 7, 9, 10)),
    # Middle Eastern, 12-TET approximations
    Scale("hijaz", "Hijaz / Phrygian Dominant", _ME, (0, 1, 4, 5, 7, 8, 10)),
    Scale("double_harmonic", "Double Harmonic / Hijaz Kar", _ME, (0, 1, 4, 5, 7, 8, 11)),
    Scale("nahawand", "Nahawand", _ME, (0, 2, 3, 5, 7, 8, 11)),
    Scale("kurd", "Kurd", _ME, (0, 1, 3, 5, 7, 8, 10)),
    Scale("nikriz", "Nikriz", _ME, (0, 2, 3, 6, 7, 9, 10)),
    Scale("persian", "Persian", _ME, (0, 1, 4, 5, 6, 8, 11)),
    Scale("arabic", "Arabic (Major Locrian)", _ME, (0, 2, 4, 5, 6, 8, 10)),
    # Arabic maqamat with quarter tones
    Scale("rast", "Rast", _MQ, (0, 2, 3.5, 5, 7, 9, 10.5)),
    Scale("bayati", "Bayati", _MQ, (0, 1.5, 3, 5, 7, 8, 10)),
    Scale("saba", "Saba", _MQ, (0, 1.5, 3, 4, 7, 8, 10)),
    Scale("sikah", "Sikah", _MQ, (0, 1.5, 3.5, 5.5, 7, 8.5, 10.5)),
    # Eastern European
    Scale("hungarian_minor", "Hungarian Minor", _EE, (0, 2, 3, 6, 7, 8, 11)),
    Scale("ukrainian_dorian", "Ukrainian Dorian", _EE, (0, 2, 3, 6, 7, 9, 10)),
    # South Asian
    Scale("bhairav", "Bhairav", _SA, (0, 1, 4, 5, 7, 8, 11)),
    Scale("bhairavi", "Bhairavi", _SA, (0, 1, 3, 5, 7, 8, 10)),
    Scale("todi", "Todi", _SA, (0, 1, 3, 6, 7, 8, 11)),
    Scale("purvi", "Purvi", _SA, (0, 1, 4, 6, 7, 8, 11)),
    Scale("marwa", "Marwa", _SA, (0, 1, 4, 6, 7, 9, 11)),
    Scale("kalyan", "Kalyan (Yaman)", _SA, (0, 2, 4, 6, 7, 9, 11)),
    Scale("khamaj", "Khamaj", _SA, (0, 2, 4, 5, 7, 9, 10)),
    Scale("kafi", "Kafi", _SA, (0, 2, 3, 5, 7, 9, 10)),
    Scale("asavari", "Asavari", _SA, (0, 2, 3, 5, 7, 8, 10)),
    # East Asian
    Scale("hirajoshi", "Hirajoshi", _EA, (0, 2, 3, 7, 8)),
    Scale("in", "In (Miyako-bushi)", _EA, (0, 1, 5, 7, 8)),
    Scale("yo", "Yo", _EA, (0, 2, 5, 7, 9)),
    Scale("iwato", "Iwato", _EA, (0, 1, 5, 6, 10)),
    Scale("kumoi", "Kumoi", _EA, (0, 2, 3, 7, 9)),
    Scale("ryukyu", "Ryukyu", _EA, (0, 4, 5, 7, 11)),
    Scale("gong", "Chinese Gong", _EA, (0, 2, 4, 7, 9)),
    Scale("shang", "Chinese Shang", _EA, (0, 2, 5, 7, 10)),
    Scale("jue", "Chinese Jue", _EA, (0, 3, 5, 8, 10)),
    Scale("yu", "Chinese Yu", _EA, (0, 3, 5, 7, 10)),
    # Southeast Asian
    Scale("pelog", "Pelog (Selisir, approx.)", _SEA, (0, 1, 3, 7, 8)),
    Scale("slendro", "Slendro (5-equal, microtonal)", _SEA, (0, 2.4, 4.8, 7.2, 9.6)),
)

SCALES_BY_KEY: dict[str, Scale] = {s.key: s for s in SCALES}

NOTE_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


def degree_to_semitones(scale: Scale, index: int) -> float:
    """Semitone offset of scale degree ``index`` (0-based, may exceed one octave or be negative)."""
    n = len(scale.steps)
    octave, pos = divmod(index, n)
    return octave * 12 + scale.steps[pos]


def chord_degrees(root_degree: int, size: int, stack: int) -> list[int]:
    """Scale-degree indices of a chord built by stacking every ``stack`` scale steps.

    stack=2 gives tertian chords in heptatonic scales, 3 quartal, 4 quintal.
    """
    return [root_degree + k * stack for k in range(max(1, size))]


def split_pitch(pitch: float) -> tuple[int, float]:
    """Split a fractional MIDI pitch into (note, bend in semitones) with bend in [-0.5, 0.5)."""
    note = int(round(pitch))
    return note, pitch - note


def bend_value(semitones: float, bend_range: float) -> int:
    """MIDI pitchwheel value (-8192..8191, mido convention) for a bend in semitones."""
    if bend_range <= 0:
        return 0
    value = int(round(semitones / bend_range * 8192))
    return max(-8192, min(8191, value))


def clamp_note(note: int) -> int:
    while note < 0:
        note += 12
    while note > 127:
        note -= 12
    return note
