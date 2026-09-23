"""MIDI ports and clocks."""

from __future__ import annotations

import logging
import sys
import threading
import time
from typing import Callable

import mido

from .engine import Engine

log = logging.getLogger(__name__)

VIRTUAL_PORT_NAME = "Listen to the Broadcast"


class DryRunOut:
    """Stands in for a MIDI port: records messages, optionally prints them."""

    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.sent: list[mido.Message] = []

    def send(self, msg: mido.Message) -> None:
        self.sent.append(msg)
        if len(self.sent) > 10000:
            del self.sent[:5000]
        if self.verbose and msg.type in ("note_on", "program_change", "pitchwheel"):
            print(msg, flush=True)

    def close(self) -> None:
        pass


def find_port(available: list[str], wanted: str) -> str | None:
    """Exact match first, then case-insensitive substring (Windows appends port numbers)."""
    if wanted in available:
        return wanted
    low = wanted.lower()
    for name in available:
        if low in name.lower():
            return name
    return None


def open_output(name: str | None):
    if name:
        port = find_port(mido.get_output_names(), name)
        if not port:
            raise SystemExit(f"MIDI output '{name}' not found. Available: {mido.get_output_names()}")
        return mido.open_output(port)
    if sys.platform == "win32":
        raise SystemExit(
            "Windows can't create virtual MIDI ports. Install loopMIDI, create a port, and pass "
            f"--out <name>. Available outputs: {mido.get_output_names()}"
        )
    return mido.open_output(VIRTUAL_PORT_NAME, virtual=True)


class ClockFollower:
    """Handles incoming MIDI (clock, transport, CC) from the host.

    Clock ticks drive the engine only while the ``sync`` setting is "midi_clock".
    CC messages go to ``on_cc`` regardless of sync mode.
    """

    def __init__(self, engine: Engine, port_name: str, on_cc: Callable[[int, int], None]):
        port = find_port(mido.get_input_names(), port_name)
        if not port:
            raise SystemExit(f"MIDI input '{port_name}' not found. Available: {mido.get_input_names()}")
        self.engine = engine
        self.on_cc = on_cc
        self.last_clock = 0.0
        self.bpm_estimate = 0.0
        self._port = mido.open_input(port, callback=self._on_message)
        self.name = port

    def _following(self) -> bool:
        return self.engine.settings["sync"] == "midi_clock"

    def _on_message(self, msg: mido.Message) -> None:
        if msg.type == "clock":
            now = time.perf_counter()
            if self.last_clock:
                dt = now - self.last_clock
                if 0 < dt < 0.5:
                    inst = 60.0 / (dt * 24)
                    self.bpm_estimate = inst if not self.bpm_estimate else self.bpm_estimate * 0.95 + inst * 0.05
            self.last_clock = now
            if self._following():
                self.engine.tick()
        elif msg.type == "start" and self._following():
            self.engine.start()
        elif msg.type == "continue" and self._following():
            self.engine.resume()
        elif msg.type == "stop" and self._following():
            self.engine.stop()
        elif msg.type == "control_change":
            self.on_cc(msg.control, msg.value)

    def status(self) -> dict:
        receiving = time.perf_counter() - self.last_clock < 1.0 if self.last_clock else False
        return {"port": self.name, "receiving_clock": receiving,
                "bpm": round(self.bpm_estimate, 1) if receiving else None}

    def close(self) -> None:
        self._port.close()


class InternalClock:
    """24 PPQN clock from the ``bpm`` setting. Idles while synced to MIDI clock."""

    def __init__(self, engine: Engine):
        self.engine = engine
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="ltb-clock", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)

    def _run(self) -> None:
        next_t = time.perf_counter()
        while not self._stop.is_set():
            s = self.engine.settings
            if s["sync"] != "internal":
                self._stop.wait(0.05)
                next_t = time.perf_counter()
                continue
            if not self.engine.running:
                self.engine.resume()
            next_t += 60.0 / (s["bpm"] * 24)
            delay = next_t - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
            elif delay < -0.2:  # fell far behind (system sleep, debugger): resync instead of bursting
                next_t = time.perf_counter()
            self.engine.tick()
