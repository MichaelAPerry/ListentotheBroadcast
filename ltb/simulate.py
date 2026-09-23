"""Synthetic LAN chatter, for trying mappings without a network (or on a quiet one)."""

from __future__ import annotations

import random
import threading
from typing import Callable

from .protocols import TrafficEvent

# kind -> (mean events per second, sample details)
_PROFILE = {
    "mdns": (1.5, ["query _airplay._tcp", "answer _googlecast._tcp", "query _spotify-connect._tcp"]),
    "ssdp": (0.6, ["NOTIFY upnp:rootdevice", "M-SEARCH ssdp:all"]),
    "llmnr": (0.15, ["query DESKTOP-4F2"]),
    "wsd": (0.08, ["Hello", "Probe"]),
    "dhcp": (0.02, ["DISCOVER", "REQUEST"]),
    "netbios": (0.1, [""]),
    "lansync": (0.5, ["Dropbox", "Spotify"]),
}


class Simulator:
    def __init__(self, on_event: Callable[[TrafficEvent], None], devices: int = 8, seed: int | None = None):
        self._on_event = on_event
        self._rng = random.Random(seed)
        self._devices = [f"192.168.1.{10 + i * 7}" for i in range(devices)]
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="ltb-sim", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)

    def _run(self) -> None:
        total = sum(rate for rate, _ in _PROFILE.values())
        kinds = list(_PROFILE)
        weights = [_PROFILE[k][0] for k in kinds]
        while not self._stop.wait(self._rng.expovariate(total)):
            kind = self._rng.choices(kinds, weights)[0]
            if self._rng.random() < 0.003:  # occasionally a new device joins
                self._devices.append(f"192.168.1.{self._rng.randint(100, 250)}")
            burst = self._rng.choice((1, 1, 1, 2, 4)) if kind == "ssdp" else 1
            for _ in range(burst):
                self._on_event(TrafficEvent(kind, self._rng.choice(self._devices),
                                            self._rng.randint(60, 900), self._rng.choice(_PROFILE[kind][1])))
