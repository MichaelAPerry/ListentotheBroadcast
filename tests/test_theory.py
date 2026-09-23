from ltb.theory import SCALES, SCALES_BY_KEY, bend_value, chord_degrees, degree_to_semitones, split_pitch


def test_scales_are_well_formed():
    assert len(SCALES_BY_KEY) == len(SCALES)
    for s in SCALES:
        assert s.steps[0] == 0
        assert list(s.steps) == sorted(s.steps)
        assert all(0 <= x < 12 for x in s.steps)


def test_scale_families_cover_requested_traditions():
    families = {s.family for s in SCALES}
    assert {"Western", "Middle Eastern (12-TET)", "Arabic maqam (quarter-tone)", "East Asian",
            "South Asian (Hindustani thaat)", "Southeast Asian"} <= families


def test_degree_wraps_octaves():
    major = SCALES_BY_KEY["major"]
    assert degree_to_semitones(major, 7) == 12
    assert degree_to_semitones(major, 9) == 16
    assert degree_to_semitones(major, -1) == -1


def test_tertian_and_quartal_chords():
    major = SCALES_BY_KEY["major"]
    triad = [degree_to_semitones(major, d) for d in chord_degrees(0, 3, 2)]
    assert triad == [0, 4, 7]
    quartal = [degree_to_semitones(major, d) for d in chord_degrees(1, 3, 3)]
    assert quartal == [2, 7, 12]


def test_quarter_tone_split_and_bend():
    rast = SCALES_BY_KEY["rast"]
    assert rast.microtonal
    note, bend = split_pitch(60 + 3.5)
    assert note + bend == 63.5
    assert abs(bend) == 0.5
    assert bend_value(0.5, 2.0) == 2048
    assert bend_value(-0.5, 2.0) == -2048
    assert bend_value(5, 2.0) == 8191
