"""Unprivileged broadcast/multicast listeners.

Plain UDP sockets with address reuse so they coexist with the OS's own mDNS/SSDP
services. No packet capture, no root/admin, no Npcap. Sockets that can't bind
(e.g. a port the OS holds exclusively) are reported and skipped.
"""

from __future__ import annotations

import logging
import selectors
import socket
import struct
import threading
from typing import Callable

from .protocols import PROTOCOLS, Protocol, TrafficEvent, describe

log = logging.getLogger(__name__)


def open_socket(port: int, group: str | None, interface: str = "0.0.0.0") -> socket.socket:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if hasattr(socket, "SO_REUSEPORT"):
            try:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            except OSError:
                pass
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.bind(("", port))
        if group:
            mreq = struct.pack("4s4s", socket.inet_aton(group), socket.inet_aton(interface))
            s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        s.setblocking(False)
        return s
    except OSError:
        s.close()
        raise


class BroadcastListener:
    def __init__(self, on_event: Callable[[TrafficEvent], None], interface: str = "0.0.0.0",
                 protocols: tuple[Protocol, ...] = PROTOCOLS):
        self._on_event = on_event
        self._interface = interface
        self._protocols = protocols
        self._selector = selectors.DefaultSelector()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        # "kind:port" -> "ok" or error text, shown in the control panel
        self.status: dict[str, str] = {}

    def start(self) -> None:
        for proto in self._protocols:
            for port in proto.ports:
                name = f"{proto.key}:{port}"
                try:
                    sock = open_socket(port, proto.group, self._interface)
                except OSError as e:
                    self.status[name] = f"unavailable ({e.strerror or e})"
                    log.warning("cannot listen on %s: %s", name, e)
                    continue
                self._selector.register(sock, selectors.EVENT_READ, (proto.key, port))
                self.status[name] = "ok"
        self._thread = threading.Thread(target=self._run, name="ltb-listener", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        for key in list(self._selector.get_map().values()):
            key.fileobj.close()
        self._selector.close()

    def _run(self) -> None:
        while not self._stop.is_set():
            if not self._selector.get_map():
                self._stop.wait(0.5)
                continue
            for key, _ in self._selector.select(timeout=0.25):
                kind, port = key.data
                try:
                    payload, (src, _sport) = key.fileobj.recvfrom(9000)
                except OSError:
                    continue
                self._on_event(TrafficEvent(kind, src, len(payload), describe(kind, payload, port)))
