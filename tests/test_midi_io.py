import mido

from ltb import midi_io
from ltb.app import App


class FakePort:
    def __init__(self, name):
        self.name, self.sent, self.closed = name, [], False

    def send(self, msg):
        self.sent.append(msg)

    def close(self):
        self.closed = True


def _fake_ports(monkeypatch, names):
    opened = []

    def open_output(name, virtual=False):
        port = FakePort(name)
        opened.append(port)
        return port

    monkeypatch.setattr(midi_io, "output_names", lambda: list(names))
    monkeypatch.setattr(mido, "open_output", open_output)
    return opened


def test_auto_output_prefers_loopmidi_style_names(monkeypatch):
    _fake_ports(monkeypatch, ["Microsoft GS Wavetable Synth 0", "loopMIDI Port 1", "LTB 2"])
    monkeypatch.setattr(midi_io.sys, "platform", "win32")
    assert midi_io.auto_output() == "LTB 2"


def test_auto_output_none_on_windows_without_loopback(monkeypatch):
    _fake_ports(monkeypatch, ["Microsoft GS Wavetable Synth 0"])
    monkeypatch.setattr(midi_io.sys, "platform", "win32")
    assert midi_io.auto_output() is None


def test_switchable_out_switches_and_closes_old(monkeypatch):
    opened = _fake_ports(monkeypatch, ["LTB 1", "Other 2"])
    out = midi_io.SwitchableOut()
    out.send(mido.Message("note_on"))  # no port yet: silently dropped
    assert out.open("ltb") == "LTB 1"
    out.send(mido.Message("note_on", note=60))
    assert out.open("Other") == "Other 2"
    assert opened[0].closed and len(opened[0].sent) == 1


def test_app_falls_back_to_auto_and_remembers(monkeypatch, tmp_path):
    _fake_ports(monkeypatch, ["LTB 1"])
    monkeypatch.setattr(midi_io.sys, "platform", "win32")
    app = App(midi_io.SwitchableOut(), tmp_path / "s.json")
    assert app.connect_output("missing port") .startswith("Couldn't open")
    assert app.connect_output(None) == ""
    assert app.out_name == "LTB 1" and app.settings["midi_out"] == "LTB 1"
    app.close()


def test_app_reports_when_nothing_available(monkeypatch, tmp_path):
    _fake_ports(monkeypatch, [])
    monkeypatch.setattr(midi_io.sys, "platform", "win32")
    app = App(midi_io.SwitchableOut(), tmp_path / "s.json")
    assert "No MIDI output" in app.connect_output(None)
    assert app.out_name == ""
    app.close()


def test_reselecting_same_port_does_not_reopen(monkeypatch):
    opened = _fake_ports(monkeypatch, ["LTB 1"])
    out = midi_io.SwitchableOut()
    out.open("LTB")
    out.open("LTB 1")
    assert len(opened) == 1 and not opened[0].closed


def test_old_port_closed_before_new_one_opens(monkeypatch):
    events = []

    class Port(FakePort):
        def close(self):
            events.append(("close", self.name))

    monkeypatch.setattr(midi_io, "output_names", lambda: ["A 1", "B 2"])
    monkeypatch.setattr(mido, "open_output", lambda name, virtual=False: events.append(("open", name)) or Port(name))
    out = midi_io.SwitchableOut()
    out.open("A")
    out.open("B")
    assert events == [("open", "A 1"), ("close", "A 1"), ("open", "B 2")]


def test_send_counts_and_records_errors(monkeypatch):
    _fake_ports(monkeypatch, ["LTB 1"])
    out = midi_io.SwitchableOut()
    out.open("LTB")
    out.send(mido.Message("note_on"))
    assert out.sent == 1

    def boom(msg):
        raise OSError("device unplugged")

    out._port.send = boom
    try:
        out.send(mido.Message("note_on"))
    except OSError:
        pass
    assert "device unplugged" in out.last_error and out.sent == 1
