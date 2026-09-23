"""Wires settings, engine, traffic sources, MIDI and the control panel together."""

from __future__ import annotations

import logging
import queue
import threading
from pathlib import Path
from typing import Any

from . import settings as settings_mod
from .engine import Engine
from .midi_io import ClockFollower, SwitchableOut, auto_output, input_names, output_choices

log = logging.getLogger(__name__)


class FeedHub:
    """Fan-out of engine/traffic messages to control-panel clients. Never blocks the publisher."""

    def __init__(self):
        self._clients: set[queue.Queue] = set()
        self._lock = threading.Lock()

    def subscribe(self) -> queue.Queue:
        q: queue.Queue = queue.Queue(maxsize=256)
        with self._lock:
            self._clients.add(q)
        return q

    def unsubscribe(self, q: queue.Queue) -> None:
        with self._lock:
            self._clients.discard(q)

    def publish(self, msg: dict[str, Any]) -> None:
        with self._lock:
            clients = list(self._clients)
        for q in clients:
            try:
                q.put_nowait(msg)
            except queue.Full:
                pass  # slow browser tab: drop, don't stall the audio side


class App:
    def __init__(self, out, settings_path: Path | None):
        self.settings_path = settings_path
        initial = settings_mod.load(settings_path) if settings_path else settings_mod.default_settings()
        self.hub = FeedHub()
        self.out = out  # SwitchableOut, or DryRunOut for --dry-run
        self.engine = Engine(out, initial, feed=self.hub.publish)
        self.dry_run = not isinstance(out, SwitchableOut)
        self.listener = None  # BroadcastListener, when not simulating
        self.clock_follower = None  # ClockFollower, when --clock-in is given
        self.simulating = False
        self._lock = threading.Lock()
        self._dirty = threading.Event()
        self._closing = threading.Event()
        self._saver = threading.Thread(target=self._save_loop, name="ltb-save", daemon=True)
        self._saver.start()

    @property
    def settings(self) -> dict[str, Any]:
        return self.engine.settings

    def update_settings(self, update: dict[str, Any]) -> dict[str, Any]:
        with self._lock:
            new = settings_mod.apply_update(self.engine.settings, update)
            self.engine.settings = new  # atomic reference swap; the engine reads it per tick
        self._dirty.set()
        self.hub.publish({"type": "settings", "settings": new})
        return new

    @property
    def out_name(self) -> str:
        return "dry run (console)" if self.dry_run else self.out.name

    # ------------------------------------------------------------ MIDI ports

    def connect_output(self, wanted: str | None = None) -> str:
        """Open ``wanted``, else the saved port, else an auto-detected one. Returns an error or ""."""
        if self.dry_run:
            return ""
        for candidate in (wanted, self.settings["midi_out"], auto_output()):
            if not candidate:
                continue
            try:
                self.engine.panic()
                name = self.out.open(candidate)
            except Exception as e:  # port vanished or is exclusively held by another app
                log.warning("cannot open MIDI output %s: %s", candidate, e)
                if candidate == wanted:
                    return f"Couldn't open '{wanted}': {e}"
                continue
            if name != self.settings["midi_out"]:
                self.update_settings({"midi_out": name})
            return ""
        return "No MIDI output found. Create a loopMIDI port, then press Refresh."

    def connect_clock(self, wanted: str) -> str:
        if self.clock_follower:
            self.clock_follower.close()
            self.clock_follower = None
        if wanted:
            try:
                self.clock_follower = ClockFollower(self.engine, wanted, self.handle_cc)
            except Exception as e:
                log.warning("cannot open MIDI input %s: %s", wanted, e)
                return f"Couldn't open '{wanted}': {e}"
        if wanted != self.settings["clock_in"]:
            self.update_settings({"clock_in": wanted})
        return ""

    def set_ports(self, body: dict[str, Any]) -> dict[str, Any]:
        error = ""
        if isinstance(body.get("midi_out"), str) and not self.dry_run:
            error = self.connect_output(body["midi_out"]) if body["midi_out"] else ""
            if not body["midi_out"]:
                self.engine.panic()
                self.out.open("")
                self.update_settings({"midi_out": ""})
        if isinstance(body.get("clock_in"), str):
            error = self.connect_clock(body["clock_in"]) or error
        return {**self.ports(), "error": error}

    def ports(self) -> dict[str, Any]:
        return {
            "outputs": [] if self.dry_run else output_choices(),
            "inputs": [n for n in input_names() if n != self.out_name],
            "midi_out": self.out_name,
            "clock_in": self.clock_follower.name if self.clock_follower else "",
            "dry_run": self.dry_run,
        }

    # -------------------------------------------------------------- settings

    def reset_settings(self) -> dict[str, Any]:
        with self._lock:
            ports = {k: self.settings[k] for k in ("midi_out", "clock_in")}
            self.engine.settings = settings_mod.apply_update(settings_mod.default_settings(), ports)
        self._dirty.set()
        self.hub.publish({"type": "settings", "settings": self.engine.settings})
        return self.engine.settings

    def handle_cc(self, control: int, value: int) -> None:
        target = self.settings["cc_map"].get(str(control))
        if target:
            self.update_settings({target: settings_mod.CC_TARGETS[target](value)})

    def state(self) -> dict[str, Any]:
        return {
            "settings": self.settings,
            "engine": self.engine.snapshot(),
            "listeners": self.listener.status if self.listener else {},
            "simulating": self.simulating,
            "midi_out": self.out_name,
            "midi_sent": len(self.out.sent) if self.dry_run else self.out.sent,
            "midi_error": getattr(self.out, "last_error", ""),
            "clock_in": self.clock_follower.status() if self.clock_follower else None,
        }

    def _save_loop(self) -> None:
        while not self._closing.is_set():
            if self._dirty.wait(1.0) and self.settings_path:
                self._closing.wait(0.5)  # debounce slider drags
                self._dirty.clear()
                try:
                    settings_mod.save(self.settings_path, self.settings)
                except OSError as e:
                    log.warning("could not save settings: %s", e)

    def close(self) -> None:
        self._closing.set()
        if self.clock_follower:
            self.clock_follower.close()
        if self.settings_path:
            try:
                settings_mod.save(self.settings_path, self.settings)
            except OSError:
                pass
