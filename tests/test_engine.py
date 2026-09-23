import random

from ltb import settings as S
from ltb.engine import TICKS_PER_BAR, Engine
from ltb.midi_io import DryRunOut
from ltb.protocols import TrafficEvent
from ltb.theory import SCALES_BY_KEY


def make(update=None, now=lambda: 0.0):
    out = DryRunOut()
    s = S.apply_update(S.default_settings(), update or {})
    feed = []
    return Engine(out, s, feed=feed.append, rng=random.Random(1), clock=now), out, feed


def run(engine, ticks):
    for _ in range(ticks):
        engine.tick()


def note_ons(out):
    return [m for m in out.sent if m.type == "note_on" and m.velocity > 0]


def test_notes_land_on_grid_and_in_scale():
    only_mdns = {k: {"enabled": k == "mdns"} for k in S.DEFAULTS["routing"]}
    e, out, _ = make({"scale": "hirajoshi", "root": 0, "microtones": "round",
                      "routing": {**only_mdns, "mdns": {"enabled": True, "division": 4, "probability": 1, "density": 16}}})
    run(e, 3)  # mid-step
    e.submit(TrafficEvent("mdns", "10.0.0.5", 100))
    assert not note_ons(out)
    run(e, 3)  # reaches tick 6 (a 16th) but mdns plays on quarters
    assert not note_ons(out)
    run(e, 19)  # processes tick 24 = beat 2
    ons = note_ons(out)
    assert len(ons) == 1
    pcs = {int(x) % 12 for x in SCALES_BY_KEY["hirajoshi"].steps}
    assert ons[0].note % 12 in pcs
    assert ons[0].channel == 0


def test_same_device_same_note_different_devices_vary():
    only_mdns = {k: {"enabled": k == "mdns"} for k in S.DEFAULTS["routing"]}
    e, out, _ = make({"progression": "hold", "routing": {**only_mdns, "mdns": {"enabled": True, "division": 1,
                                                                             "probability": 1, "density": 16}}})
    notes = {}
    for src in ["10.0.0.1", "10.0.0.1", "10.0.0.2", "10.0.0.3", "10.0.0.4"]:
        e.submit(TrafficEvent("mdns", src, 100))
        before = len(note_ons(out))
        run(e, 6)
        notes.setdefault(src, set()).add(note_ons(out)[before].note)
    assert len(notes["10.0.0.1"]) == 1
    assert len({n for v in notes.values() for n in v}) > 1


def test_every_note_on_gets_a_note_off():
    e, out, _ = make()
    for i in range(200):
        e.submit(TrafficEvent(["mdns", "ssdp", "dhcp", "netbios", "lansync", "llmnr", "wsd"][i % 7], f"10.0.0.{i % 9}", 500))
        run(e, 7)
    run(e, TICKS_PER_BAR * 20)  # let everything release
    on = {}
    for m in out.sent:
        key = (m.channel, getattr(m, "note", None))
        if m.type == "note_on" and m.velocity > 0:
            on[key] = on.get(key, 0) + 1
        elif m.type == "note_off":
            on[key] = max(0, on.get(key, 0) - 1)
    assert not any(on.values())
    assert not e._active


def test_density_cap_limits_notes_per_bar():
    only = {k: {"enabled": k == "llmnr"} for k in S.DEFAULTS["routing"]}
    e, out, _ = make({"routing": {**only, "llmnr": {"enabled": True, "division": 1, "density": 2, "probability": 1}}})
    for _ in range(16):
        e.submit(TrafficEvent("llmnr", "10.0.0.9", 60))
        run(e, 6)
    assert len(note_ons(out)) == 2


def test_chord_progression_advances_on_bars():
    e, _, feed = make({"progression": "pop (I-V-vi-IV)", "change_bars": 1, "change_mode": "bars"})
    run(e, TICKS_PER_BAR * 4 + 1)
    chords = [m["degree"] for m in feed if m["type"] == "chord"]
    assert chords == [4, 5, 3, 0]


def test_new_device_mode_changes_chord_after_warmup():
    clock = {"t": 0.0}
    e, _, feed = make({"change_mode": "new_device"}, now=lambda: clock["t"])
    e.submit(TrafficEvent("mdns", "10.0.0.1", 60))  # during warm-up: not "new"
    run(e, TICKS_PER_BAR + 1)
    assert not [m for m in feed if m["type"] == "chord"]
    clock["t"] = 100.0
    e.submit(TrafficEvent("mdns", "10.0.0.99", 60))
    run(e, TICKS_PER_BAR)
    assert [m for m in feed if m["type"] == "chord"]


def test_quarter_tones_use_pitch_bend():
    only = {k: {"enabled": k == "mdns"} for k in S.DEFAULTS["routing"]}
    e, out, _ = make({"scale": "rast", "microtones": "bend", "chord_degree": 2,
                      "routing": {**only, "mdns": {"enabled": True, "division": 1, "probability": 1}}})
    # find a device whose motif lands on a quarter-tone degree
    for i in range(50):
        e.submit(TrafficEvent("mdns", f"10.1.0.{i}", 100))
        run(e, 6)
    bends = [m.pitch for m in out.sent if m.type == "pitchwheel"]
    assert 2048 in bends or -2048 in bends


def test_pad_sustains_and_revoices_on_chord_change():
    only = {k: {"enabled": k == "ssdp"} for k in S.DEFAULTS["routing"]}
    e, out, _ = make({"change_bars": 2, "progression": "pop (I-V-vi-IV)",
                      "routing": {**only, "ssdp": {"enabled": True, "length": 256}}})
    for _ in range(4 * 16):
        e.submit(TrafficEvent("ssdp", "10.0.0.7", 300))
        run(e, 6)
    ons = note_ons(out)
    # voiced at bar 1, then re-voiced at each chord change (bars 3)
    assert 3 * 2 <= len(ons) <= 3 * 3


def test_panic_silences_everything():
    e, out, _ = make()
    e.submit(TrafficEvent("dhcp", "10.0.0.1", 300))
    run(e, TICKS_PER_BAR + 1)
    assert e._active
    e.panic()
    assert not e._active
    assert any(m.type == "control_change" and m.control == 123 for m in out.sent)


def test_midi_start_rewinds():
    e, _, _ = make()
    run(e, TICKS_PER_BAR * 3 + 10)
    e.start()
    assert e.tick_count == 0 and e.bar == 0
    e.stop()
    run(e, 50)
    assert e.tick_count == 0


def test_test_note_plays_immediately_and_releases():
    e, out, _ = make()
    e.test_note(3)
    ons = note_ons(out)
    assert len(ons) == 1 and ons[0].channel == 2 and ons[0].note == 60
    run(e, 30)
    assert not e._active
    assert any(m.type == "note_off" and m.channel == 2 for m in out.sent)


def test_snapshot_counts_packets_and_notes():
    only = {k: {"enabled": k == "mdns"} for k in S.DEFAULTS["routing"]}
    e, _, _ = make({"routing": {**only, "mdns": {"enabled": True, "division": 1, "probability": 1}}})
    e.submit(TrafficEvent("mdns", "10.0.0.1", 60))
    snap = e.snapshot()
    assert snap["packets"]["mdns"] == 1 and snap["activity"]["mdns"] > 0.3
    run(e, 7)
    assert e.snapshot()["notes"]["mdns"] == 1
