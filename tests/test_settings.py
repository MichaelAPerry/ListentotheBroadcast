from ltb import settings as S


def test_defaults_validate_to_themselves():
    d = S.default_settings()
    assert S.validate(d) == d


def test_partial_update_and_clamping():
    d = S.default_settings()
    u = S.apply_update(d, {"bpm": 9999, "routing": {"mdns": {"channel": 40, "role": "chord"}}})
    assert u["bpm"] == 300
    assert u["routing"]["mdns"]["channel"] == 16
    assert u["routing"]["mdns"]["role"] == "chord"
    assert u["routing"]["ssdp"] == d["routing"]["ssdp"]
    assert d["bpm"] == 84.0  # input not mutated


def test_rejects_bad_types():
    d = S.default_settings()
    u = S.apply_update(d, {"scale": "nope", "routing": {"mdns": "x"}, "cc_map": {"300": "bpm", "5": "evil"},
                           "division": 3, "swing": "NaN"})
    assert u["scale"] == d["scale"]
    assert u["routing"]["mdns"] == d["routing"]["mdns"]
    assert u["cc_map"] == {}
    assert u["swing"] == d["swing"]


def test_cc_targets_produce_valid_values():
    d = S.default_settings()
    for target, fn in S.CC_TARGETS.items():
        for v in (0, 64, 127):
            u = S.apply_update(d, {target: fn(v)})
            assert u[target] == S.validate({**d, target: fn(v)})[target]
    assert S.CC_TARGETS["root"](127) == 11
    assert S.CC_TARGETS["scale"](127) in S.SCALES_BY_KEY


def test_load_and_save_roundtrip(tmp_path):
    p = tmp_path / "s.json"
    s = S.apply_update(S.default_settings(), {"scale": "hijaz"})
    S.save(p, s)
    assert S.load(p)["scale"] == "hijaz"
    p.write_text("{not json")
    assert S.load(p) == S.default_settings()
