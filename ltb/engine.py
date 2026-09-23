"""The music engine: turns queued traffic events into quantized, scale-aware MIDI.

Time advances in MIDI-clock ticks (24 per quarter note), driven either by the
internal clock or by incoming MIDI clock from the host. Events are queued as
they arrive and only played on the rhythmic grid, so network jitter becomes
musical placement. The queues are lossy by design: if traffic outruns the
per-type density cap, excess events are dropped rather than piling up.
"""

from __future__ import annotations

import hashlib
import math
import random
import threading
import time
from collections import deque
from typing import Any, Callable

import mido

from .protocols import PROTOCOLS_BY_KEY, TrafficEvent
from .settings import PROGRESSIONS
from .theory import SCALES_BY_KEY, NOTE_NAMES, bend_value, chord_degrees, clamp_note, degree_to_semitones, split_pitch

TICKS_PER_STEP = 6  # 16th notes at 24 PPQN
STEPS_PER_BAR = 16
TICKS_PER_BAR = TICKS_PER_STEP * STEPS_PER_BAR
NEW_DEVICE_WARMUP_S = 15.0  # devices seen right after startup don't count as "new"

Feed = Callable[[dict[str, Any]], None]


def stable_hash(text: str) -> int:
    return int.from_bytes(hashlib.blake2s(text.encode(), digest_size=4).digest(), "big")


class Engine:
    def __init__(self, out, settings: dict[str, Any], feed: Feed | None = None,
                 rng: random.Random | None = None, clock: Callable[[], float] | None = None):
        self.out = out
        self.settings = settings
        self.feed: Feed = feed or (lambda _msg: None)
        self._rng = rng or random.Random()
        self._now = clock or time.monotonic
        self._started_at = self._now()
        self._lock = threading.RLock()

        self._pending: dict[str, deque[TrafficEvent]] = {k: deque(maxlen=64) for k in PROTOCOLS_BY_KEY}
        self._packets: dict[str, int] = {k: 0 for k in PROTOCOLS_BY_KEY}
        self._notes: dict[str, int] = {k: 0 for k in PROTOCOLS_BY_KEY}
        self.notes_played = 0
        self._activity: dict[str, float] = {k: 0.0 for k in PROTOCOLS_BY_KEY}
        self._last_cc: dict[int, int] = {}
        self._devices: set[str] = set()
        self._new_device = False

        self.tick_count = 0
        self.bar = 0
        self.running = True
        self._fires_this_bar: dict[str, int] = {}
        self._active: dict[tuple[int, int], int] = {}  # (channel0, note) -> off tick
        self._pad_notes: dict[str, list[tuple[int, int]]] = {}
        self._bend: dict[int, int] = {}
        self._program: dict[int, int] = {}
        self._prog_index = 0
        self._walk = 0
        self._chord_changed = True

    # ------------------------------------------------------------------ input

    def submit(self, event: TrafficEvent) -> None:
        """Called from the listener thread for every packet."""
        with self._lock:
            if event.kind not in self._pending:
                return
            self._pending[event.kind].append(event)
            self._activity[event.kind] += 1.0
            self._packets[event.kind] += 1
            if event.src not in self._devices:
                self._devices.add(event.src)
                if self._now() - self._started_at > NEW_DEVICE_WARMUP_S:
                    self._new_device = True
                    self.feed({"type": "device", "src": event.src, "kind": event.kind})
            self.feed({"type": "packet", "kind": event.kind, "src": event.src,
                       "size": event.size, "detail": event.detail})

    # --------------------------------------------------------------- transport

    def start(self) -> None:
        """MIDI Start: rewind to bar 1 and play."""
        with self._lock:
            self.panic()
            self.tick_count = 0
            self.bar = 0
            self._prog_index = 0
            self._walk = 0
            self._chord_changed = True
            self.running = True

    def resume(self) -> None:
        with self._lock:
            self.running = True

    def stop(self) -> None:
        with self._lock:
            self.running = False
            self.panic()

    def panic(self) -> None:
        with self._lock:
            for ch, note in list(self._active):
                self._send(mido.Message("note_off", channel=ch, note=note, velocity=0))
            self._active.clear()
            self._pad_notes.clear()
            for ch in range(16):
                self._send(mido.Message("control_change", channel=ch, control=64, value=0))
                self._send(mido.Message("control_change", channel=ch, control=123, value=0))
            for ch in list(self._bend):
                self._send(mido.Message("pitchwheel", channel=ch, pitch=0))
            self._bend.clear()

    # ------------------------------------------------------------------ clock

    def tick(self) -> None:
        """Advance one MIDI-clock tick (1/24 quarter note)."""
        with self._lock:
            if not self.running:
                return
            s = self.settings
            t = self.tick_count
            self._release_due(t)

            tick_in_bar = t % TICKS_PER_BAR
            if tick_in_bar == 0:
                if t > 0:
                    self.bar += 1
                    self._maybe_change_chord(s)
                self._fires_this_bar = {}

            step, offset = divmod(tick_in_bar, TICKS_PER_STEP)
            swing_ticks = int(round(s["swing"] * 3))
            if offset == (swing_ticks if step % 2 else 0):
                self._on_step(s, step)
            self.tick_count += 1

    # ---------------------------------------------------------------- harmony

    def chord_root_degree(self, s: dict[str, Any] | None = None) -> int:
        s = s or self.settings
        prog = PROGRESSIONS.get(s["progression"]) or [0]
        if s["progression"] == "random walk":
            offset = self._walk
        else:
            offset = prog[self._prog_index % len(prog)]
        return s["chord_degree"] + offset

    def _maybe_change_chord(self, s: dict[str, Any]) -> None:
        if s["change_mode"] == "bars":
            due = self.bar % s["change_bars"] == 0
        else:
            due = self._new_device
        self._new_device = False
        if not due:
            return
        if s["progression"] == "random walk":
            self._walk = (self._walk + self._rng.choice((-2, -1, 1, 2, 3))) % 7
        elif PROGRESSIONS.get(s["progression"]):
            self._prog_index += 1
        else:
            return
        self._chord_changed = True
        scale = SCALES_BY_KEY[s["scale"]]
        root_pc = int(round(s["root"] + degree_to_semitones(scale, self.chord_root_degree(s)))) % 12
        self.feed({"type": "chord", "bar": self.bar, "degree": self.chord_root_degree(s),
                   "root": NOTE_NAMES[root_pc]})

    def _chord_pitches(self, s: dict[str, Any], base: float) -> list[float]:
        scale = SCALES_BY_KEY[s["scale"]]
        root_deg = self.chord_root_degree(s)
        pitches = []
        for i, deg in enumerate(chord_degrees(root_deg, s["chord_size"], s["chord_stack"])):
            spread = 12 * (i % (s["chord_spread"] + 1)) if i else 0
            pitches.append(base + degree_to_semitones(scale, deg) + spread)
        return pitches

    # ------------------------------------------------------------------- play

    def _on_step(self, s: dict[str, Any], step: int) -> None:
        for kind in self._activity:
            self._activity[kind] *= 0.97
        budget = s["max_notes_per_step"]
        chord_changed = self._chord_changed
        for kind, r in s["routing"].items():
            if budget <= 0:
                break
            if not r["enabled"]:
                self._pending[kind].clear()
                continue
            if step % 4 == 0:
                self._send_rate_cc(s, kind, r)
            if r["role"] == "pad":
                budget -= self._maybe_pad(s, kind, r, step, chord_changed)
                continue
            if step % r["division"]:
                continue
            events = self._pending[kind]
            if not events:
                continue
            if self._fires_this_bar.get(kind, 0) >= r["density"] or self._rng.random() > r["probability"]:
                events.clear()
                continue
            batch = list(events)
            events.clear()
            budget -= self._fire(s, kind, r, batch, budget)
        if chord_changed:
            self._chord_changed = False

    def _maybe_pad(self, s, kind, r, step, chord_changed) -> int:
        # Pads breathe with traffic: (re)voice at bar starts while this traffic type is active.
        events = self._pending[kind]
        if step != 0:
            return 0
        sounding = any(k in self._active for k in self._pad_notes.get(kind, []))
        active = bool(events) or self._activity[kind] > 0.5
        events.clear()
        if not active or (sounding and not chord_changed):
            return 0
        return self._fire(s, kind, r, [], s["max_notes_per_step"], pad=True)

    def _fire(self, s, kind, r, batch: list[TrafficEvent], budget: int, pad: bool = False) -> int:
        scale = SCALES_BY_KEY[s["scale"]]
        ch = r["channel"] - 1
        base = 12 * (s["base_octave"] + 1) + s["root"] + 12 * r["octave"]
        count = len(batch)
        vel = r["velocity"] * s["master_velocity"] * (0.75 + 0.25 * min(1.0, count / 4)) if count else \
            r["velocity"] * s["master_velocity"]
        vel = max(1, min(127, int(round(vel))))
        role = r["role"]
        latest = batch[-1] if batch else None

        if role == "hit":
            pitches = [float(r["hit_note"])]
        elif role == "bass":
            pitches = [base - 12 + degree_to_semitones(scale, self.chord_root_degree(s))]
        elif role in ("chord", "pad"):
            pitches = self._chord_pitches(s, base)
        else:  # note: each device gets a stable scale degree relative to the current chord
            motif = stable_hash(latest.src) % len(scale.steps)
            pitch = base + degree_to_semitones(scale, self.chord_root_degree(s) + motif)
            if latest.size > 400:
                pitch += 12
            pitches = [pitch]

        pitches = pitches[:max(0, budget)]
        if not pitches:
            return 0

        if r["program"] >= 0 and self._program.get(ch) != r["program"]:
            self._send(mido.Message("program_change", channel=ch, program=r["program"]))
            self._program[ch] = r["program"]

        mono = role in ("note", "bass") and s["microtones"] == "bend"
        notes: list[int] = []
        if mono:
            note, bend = split_pitch(pitches[0])
            self._set_bend(ch, bend_value(bend, s["bend_range"]))
            notes = [clamp_note(note)]
        else:
            if role != "hit":
                self._set_bend(ch, 0)
            notes = [clamp_note(int(round(p))) for p in pitches]

        if pad:
            for key in self._pad_notes.get(kind, []):
                if key in self._active:
                    del self._active[key]
                    self._send(mido.Message("note_off", channel=key[0], note=key[1], velocity=0))
            self._pad_notes[kind] = [(ch, n) for n in notes]

        off_tick = self.tick_count + r["length"] * TICKS_PER_STEP
        for n in notes:
            key = (ch, n)
            if key in self._active:
                self._send(mido.Message("note_off", channel=ch, note=n, velocity=0))
            self._send(mido.Message("note_on", channel=ch, note=n, velocity=vel))
            self._active[key] = off_tick

        self._fires_this_bar[kind] = self._fires_this_bar.get(kind, 0) + 1
        self._notes[kind] += len(notes)
        self.notes_played += len(notes)
        self.feed({"type": "note", "kind": kind, "role": role, "channel": ch + 1, "notes": notes,
                   "velocity": vel, "count": count, "src": latest.src if latest else "",
                   "detail": latest.detail if latest else ""})
        return len(notes)

    def _set_bend(self, ch: int, value: int) -> None:
        if self._bend.get(ch, 0) != value:
            self._send(mido.Message("pitchwheel", channel=ch, pitch=value))
            self._bend[ch] = value

    def _send_rate_cc(self, s, kind, r) -> None:
        if s["rate_cc"] < 0 or r["role"] == "hit":
            return
        value = int(round(127 * (1 - math.exp(-self._activity[kind] / 8))))
        ch = r["channel"] - 1
        key = ch * 128 + s["rate_cc"]
        if self._last_cc.get(key) != value:
            self._send(mido.Message("control_change", channel=ch, control=s["rate_cc"], value=value))
            self._last_cc[key] = value

    def _release_due(self, t: int) -> None:
        for key, off in list(self._active.items()):
            if off <= t:
                del self._active[key]
                self._send(mido.Message("note_off", channel=key[0], note=key[1], velocity=0))

    def _send(self, msg: mido.Message) -> None:
        try:
            self.out.send(msg)
        except Exception:  # a vanished MIDI port must never kill the clock thread
            pass

    # ------------------------------------------------------------------ state

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            s = self.settings
            scale = SCALES_BY_KEY[s["scale"]]
            root_pc = int(round(s["root"] + degree_to_semitones(scale, self.chord_root_degree(s)))) % 12
            return {
                "bar": self.bar + 1,
                "beat": (self.tick_count % TICKS_PER_BAR) // 24 + 1,
                "running": self.running,
                "chord_root": NOTE_NAMES[root_pc],
                "chord_degree": self.chord_root_degree(s),
                "devices": len(self._devices),
                "active_notes": len(self._active),
                # Display scale: a single packet shows clearly, a busy stream fills the meter.
                "activity": {k: round(1 - math.exp(-v / 2), 3) for k, v in self._activity.items()},
                "packets": dict(self._packets),
                "notes": dict(self._notes),
                "notes_played": self.notes_played,
            }

    def test_note(self, channel: int, note: int = 60, velocity: int = 100, beats: float = 1.0) -> None:
        """Play one note right now on ``channel`` (1-16), bypassing traffic and the grid."""
        with self._lock:
            ch = max(1, min(16, int(channel))) - 1
            self._set_bend(ch, 0)
            key = (ch, note)
            if key in self._active:
                self._send(mido.Message("note_off", channel=ch, note=note, velocity=0))
            self._send(mido.Message("note_on", channel=ch, note=note, velocity=velocity))
            # Released by the clock like any other note, or by a timer if the clock is stopped.
            self._active[key] = self.tick_count + int(24 * beats)
        timer = threading.Timer(1.5, self._release_test_note, args=(key,))
        timer.daemon = True
        timer.start()

    def _release_test_note(self, key: tuple[int, int]) -> None:
        with self._lock:
            if key in self._active and not self.running:
                del self._active[key]
                self._send(mido.Message("note_off", channel=key[0], note=key[1], velocity=0))
