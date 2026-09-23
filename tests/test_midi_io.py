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
