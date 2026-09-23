"""Engine settings: defaults, validation and partial updates.

Settings are a plain JSON-compatible dict so the control panel, preset files and
MIDI CC control all share one shape. ``apply_update`` never mutates its input and
always returns a fully validated copy.
"""

from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any

from .protocols import PROTOCOLS
from .theory import SCALES_BY_KEY

ROLES = ("note", "chord", "pad", "bass", "hit")
DIVISIONS = (1, 2, 4, 8, 16)  # in 16th-note steps: 1/16, 1/8, 1/4, 1/2, 1 bar

PROGRESSIONS: dict[str, list[int]] = {
    "hold": [],
    "drift (I-IV-I-V)": [0, 3, 0, 4],
    "pop (I-V-vi-IV)": [0, 4, 5, 3],
    "rise (I-ii-iii-IV)": [0, 1, 2, 3],
    "pendulum (I-II)": [0, 1],
    "andalusian (i-VII-VI-V)": [0, 6, 5, 4],
    "random walk": [],
}

_DEFAULT_ROUTING = {
    #          role     ch prog oct div len  vel dens prob hit
    "mdns":    ("note",  1, -1,  0,  2,  4,  80,  8, 0.9, 60),
    "ssdp":    ("pad",   2, -1,  0, 16, 64,  60,  1, 1.0, 60),
    "llmnr":   ("note",  3, -1,  1,  4,  2,  70,  4, 0.8, 60),
    "wsd":     ("note",  4, -1,  1,  8,  8,  75,  2, 1.0, 60),
    "dhcp":    ("chord", 5, -1,  0, 16, 32,  90,  1, 1.0, 60),
    "netbios": ("bass",  6, -1, -1,  8, 16,  85,  2, 1.0, 60),
    "lansync": ("hit",  10, -1,  0,  4,  1,  90,  4, 1.0, 42),
}

DEFAULTS: dict[str, Any] = {
    "bpm": 84.0,
    "sync": "internal",  # "internal" or "midi_clock"
    "swing": 0.0,  # 0..1, delays odd 16ths by up to 3 clock ticks
    "root": 2,  # D
    "scale": "dorian",
    "base_octave": 4,  # MIDI octave of the root; 4 -> D4 = 62
    "microtones": "bend",  # "bend" or "round"
    "bend_range": 2.0,  # semitones; must match the synth's pitch-bend range
    "chord_degree": 0,
    "chord_size": 3,
    "chord_stack": 2,  # scale steps between chord tones (2=thirds, 3=fourths, 4=fifths)
    "chord_spread": 0,  # extra octaves to spread upper chord tones across
    "progression": "drift (I-IV-I-V)",
    "change_mode": "bars",  # "bars" or "new_device"
    "change_bars": 4,
    "master_velocity": 1.0,
    "max_notes_per_step": 6,
    "rate_cc": 1,  # CC sent per channel with smoothed traffic intensity, -1 = off
    "midi_out": "",  # last chosen MIDI output port ("" = auto-detect)
    "clock_in": "",  # last chosen clock/CC input port ("" = none)
    "routing": {
        k: {
            "enabled": True, "role": r, "channel": ch, "program": pg, "octave": o, "division": d,
            "length": ln, "velocity": v, "density": dn, "probability": p, "hit_note": h,
        }
        for k, (r, ch, pg, o, d, ln, v, dn, p, h) in _DEFAULT_ROUTING.items()
    },
    # Incoming MIDI CC number -> setting, so host automation/controllers can drive the engine.
    "cc_map": {
        "20": "root", "21": "scale", "22": "chord_degree", "23": "chord_size",
        "24": "chord_stack", "25": "bpm", "26": "master_velocity", "27": "swing",
    },
}


def _num(value: Any, lo: float, hi: float, default: float, integer: bool = False):
    try:
        x = float(value)
    except (TypeError, ValueError):
        return default
    if x != x:  # NaN
        return default
    x = max(lo, min(hi, x))
    return int(round(x)) if integer else x


def _text(value: Any) -> str:
    return value[:200] if isinstance(value, str) else ""


def _choice(value: Any, options, default):
    return value if value in options else default


def validate(s: dict[str, Any]) -> dict[str, Any]:
    d = DEFAULTS
    out: dict[str, Any] = {
        "bpm": _num(s.get("bpm"), 20, 300, d["bpm"]),
        "sync": _choice(s.get("sync"), ("internal", "midi_clock"), d["sync"]),
        "swing": _num(s.get("swing"), 0, 1, d["swing"]),
        "root": _num(s.get("root"), 0, 11, d["root"], True),
        "scale": _choice(s.get("scale"), SCALES_BY_KEY, d["scale"]),
        "base_octave": _num(s.get("base_octave"), 1, 7, d["base_octave"], True),
        "microtones": _choice(s.get("microtones"), ("bend", "round"), d["microtones"]),
        "bend_range": _num(s.get("bend_range"), 0.5, 48, d["bend_range"]),
        "chord_degree": _num(s.get("chord_degree"), 0, 6, d["chord_degree"], True),
        "chord_size": _num(s.get("chord_size"), 1, 6, d["chord_size"], True),
        "chord_stack": _num(s.get("chord_stack"), 1, 4, d["chord_stack"], True),
        "chord_spread": _num(s.get("chord_spread"), 0, 2, d["chord_spread"], True),
        "progression": _choice(s.get("progression"), PROGRESSIONS, d["progression"]),
        "change_mode": _choice(s.get("change_mode"), ("bars", "new_device"), d["change_mode"]),
        "change_bars": _num(s.get("change_bars"), 1, 32, d["change_bars"], True),
        "master_velocity": _num(s.get("master_velocity"), 0, 1.5, d["master_velocity"]),
        "max_notes_per_step": _num(s.get("max_notes_per_step"), 1, 32, d["max_notes_per_step"], True),
        "rate_cc": _num(s.get("rate_cc"), -1, 127, d["rate_cc"], True),
        "midi_out": _text(s.get("midi_out")),
        "clock_in": _text(s.get("clock_in")),
    }
    routing_in = s.get("routing")
    routing_in = routing_in if isinstance(routing_in, dict) else {}
    out["routing"] = {}
    for proto in PROTOCOLS:
        rd = d["routing"][proto.key]
        r = routing_in.get(proto.key)
        r = r if isinstance(r, dict) else {}
        div = _num(r.get("division"), 1, 16, rd["division"], True)
        out["routing"][proto.key] = {
            "enabled": bool(r.get("enabled", rd["enabled"])),
            "role": _choice(r.get("role"), ROLES, rd["role"]),
            "channel": _num(r.get("channel"), 1, 16, rd["channel"], True),
            "program": _num(r.get("program"), -1, 127, rd["program"], True),
            "octave": _num(r.get("octave"), -3, 3, rd["octave"], True),
            "division": div if div in DIVISIONS else rd["division"],
            "length": _num(r.get("length"), 1, 256, rd["length"], True),
            "velocity": _num(r.get("velocity"), 1, 127, rd["velocity"], True),
            "density": _num(r.get("density"), 1, 16, rd["density"], True),
            "probability": _num(r.get("probability"), 0, 1, rd["probability"]),
            "hit_note": _num(r.get("hit_note"), 0, 127, rd["hit_note"], True),
        }
    cc_in = s.get("cc_map")
    cc_map = cc_in if isinstance(cc_in, dict) else d["cc_map"]
    out["cc_map"] = {
        str(int(k)): v for k, v in cc_map.items()
        if str(k).isdigit() and 0 <= int(k) <= 127 and v in CC_TARGETS
    }
    return out


def _deep_merge(base: dict, update: dict) -> dict:
    merged = copy.deepcopy(base)
    for k, v in update.items():
        if isinstance(v, dict) and isinstance(merged.get(k), dict) and k != "cc_map":
            merged[k] = _deep_merge(merged[k], v)
        else:
            merged[k] = copy.deepcopy(v)
    return merged


def apply_update(current: dict[str, Any], update: dict[str, Any]) -> dict[str, Any]:
    return validate(_deep_merge(current, update))


def default_settings() -> dict[str, Any]:
    return validate(copy.deepcopy(DEFAULTS))


# CC value (0..127) -> setting value, for incoming controller/automation.
def _scale_from_cc(v: int) -> str:
    keys = list(SCALES_BY_KEY)
    return keys[min(len(keys) - 1, v * len(keys) // 128)]


CC_TARGETS = {
    "root": lambda v: v * 12 // 128,
    "scale": _scale_from_cc,
    "chord_degree": lambda v: v * 7 // 128,
    "chord_size": lambda v: 1 + v * 6 // 128,
    "chord_stack": lambda v: 1 + v * 4 // 128,
    "bpm": lambda v: 40 + v * 140 / 127,
    "master_velocity": lambda v: v / 127 * 1.5,
    "swing": lambda v: v / 127,
}


def load(path: Path) -> dict[str, Any]:
    try:
        return apply_update(default_settings(), json.loads(path.read_text()))
    except (OSError, ValueError):
        return default_settings()


def save(path: Path, s: dict[str, Any]) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(s, indent=2))
    tmp.replace(path)
