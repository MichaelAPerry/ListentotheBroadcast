import struct

from ltb.protocols import describe


def _dns_query(name: str) -> bytes:
    header = struct.pack("!HHHHHH", 0, 0, 1, 0, 0, 0)
    qname = b"".join(bytes([len(p)]) + p.encode() for p in name.split(".")) + b"\x00"
    return header + qname + struct.pack("!HH", 12, 1)


def test_mdns_service_type():
    assert describe("mdns", _dns_query("_airplay._tcp.local")) == "query _airplay._tcp"


def test_mdns_compression_pointer_answer():
    header = struct.pack("!HHHHHH", 0, 0x8400, 0, 1, 0, 0)
    name = b"\x08_spotify\x04_tcp\x05local\x00"
    pkt = header + name
    # second name uses a pointer to offset 12
    assert describe("mdns", pkt) == "answer _spotify._tcp"
    looped = header + b"\xc0\x0c"  # pointer to itself
    assert describe("mdns", looped) == ""


def test_ssdp_notify():
    pkt = b"NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nNT: upnp:rootdevice\r\n\r\n"
    assert describe("ssdp", pkt) == "NOTIFY upnp:rootdevice"


def test_dhcp_discover():
    pkt = bytearray(240)
    pkt[236:240] = b"\x63\x82\x53\x63"
    pkt += bytes([53, 1, 1, 255])
    assert describe("dhcp", bytes(pkt)) == "DISCOVER"


def test_garbage_never_raises():
    for kind in ("mdns", "llmnr", "ssdp", "wsd", "dhcp", "netbios", "lansync", "unknown"):
        describe(kind, b"\xff" * 7)
        describe(kind, b"")
