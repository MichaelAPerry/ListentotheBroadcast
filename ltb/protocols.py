"""Broadcast/multicast traffic types that an unprivileged UDP socket can hear, plus light payload summaries."""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field
import time


@dataclass(frozen=True)
class Protocol:
    key: str
    label: str
    ports: tuple[int, ...]
    group: str | None  # multicast group to join, or None for plain broadcast


PROTOCOLS: tuple[Protocol, ...] = (
    Protocol("mdns", "mDNS / Bonjour", (5353,), "224.0.0.251"),
    Protocol("ssdp", "SSDP / UPnP", (1900,), "239.255.255.250"),
    Protocol("llmnr", "LLMNR (Windows names)", (5355,), "224.0.0.252"),
    Protocol("wsd", "WS-Discovery (printers, cams)", (3702,), "239.255.255.250"),
    Protocol("dhcp", "DHCP (device joins)", (67, 68), None),
    Protocol("netbios", "NetBIOS", (137, 138), None),
    Protocol("lansync", "LAN sync beacons (Dropbox/Spotify/Steam)", (17500, 57621, 27036), None),
)

PROTOCOLS_BY_KEY: dict[str, Protocol] = {p.key: p for p in PROTOCOLS}


@dataclass
class TrafficEvent:
    kind: str
    src: str
    size: int
    detail: str = ""
    t: float = field(default_factory=time.monotonic)


def describe(kind: str, payload: bytes, port: int = 0) -> str:
    """Short human-readable summary of a packet. Never raises."""
    try:
        if kind in ("mdns", "llmnr"):
            return _describe_dns(payload)
        if kind == "ssdp":
            return _describe_ssdp(payload)
        if kind == "wsd":
            m = re.search(rb"(Hello|Bye|Probe|ProbeMatches|Resolve)\b", payload)
            return m.group(1).decode() if m else ""
        if kind == "dhcp":
            return _describe_dhcp(payload)
        if kind == "lansync":
            return {17500: "Dropbox", 57621: "Spotify", 27036: "Steam"}.get(port, "")
    except Exception:  # malformed packets are expected on a LAN
        pass
    return ""


def _read_name(buf: bytes, off: int, depth: int = 0) -> tuple[str, int]:
    labels: list[str] = []
    while True:
        if off >= len(buf) or depth > 8:
            raise ValueError("bad name")
        length = buf[off]
        if length == 0:
            return ".".join(labels), off + 1
        if length & 0xC0 == 0xC0:
            ptr = struct.unpack_from("!H", buf, off)[0] & 0x3FFF
            rest, _ = _read_name(buf, ptr, depth + 1)
            if rest:
                labels.append(rest)
            return ".".join(labels), off + 2
        labels.append(buf[off + 1 : off + 1 + length].decode("utf-8", "replace"))
        off += 1 + length


def _describe_dns(payload: bytes) -> str:
    _, flags, qd, an = struct.unpack_from("!HHHH", payload, 0)
    off = 12
    name = ""
    if qd:
        name, off = _read_name(payload, off)
    elif an:
        name, off = _read_name(payload, off)
    # Prefer the DNS-SD service type (e.g. "_airplay._tcp") when present.
    m = re.search(r"(_[\w-]+\._(?:tcp|udp))", name)
    label = m.group(1) if m else name
    return ("answer " if flags & 0x8000 else "query ") + label


def _describe_ssdp(payload: bytes) -> str:
    text = payload[:1024].decode("latin-1", "replace")
    first = text.split("\r\n", 1)[0].split(" ")[0]
    m = re.search(r"^(?:NT|ST):\s*(.+)$", text, re.MULTILINE | re.IGNORECASE)
    target = m.group(1).strip() if m else ""
    return f"{first} {target}".strip()


_DHCP_TYPES = {1: "DISCOVER", 2: "OFFER", 3: "REQUEST", 4: "DECLINE", 5: "ACK", 6: "NAK", 7: "RELEASE", 8: "INFORM"}


def _describe_dhcp(payload: bytes) -> str:
    if len(payload) < 240 or payload[236:240] != b"\x63\x82\x53\x63":
        return ""
    off = 240
    while off < len(payload):
        opt = payload[off]
        if opt == 255:
            break
        if opt == 0:
            off += 1
            continue
        length = payload[off + 1]
        if opt == 53 and length >= 1:
            return _DHCP_TYPES.get(payload[off + 2], "")
        off += 2 + length
    return ""
