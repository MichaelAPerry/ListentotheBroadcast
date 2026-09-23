import json
import urllib.request

import pytest

from ltb.app import App
from ltb.midi_io import DryRunOut
from ltb.web import ControlServer


@pytest.fixture
def server(tmp_path):
    app = App(DryRunOut(), tmp_path / "s.json")
    srv = ControlServer(app, "127.0.0.1", 0)
    srv.start()
    yield app, srv.url
    srv.stop()
    app.close()


def _get(url):
    with urllib.request.urlopen(url, timeout=5) as r:
        return r.status, r.read()


def _post(url, body, headers=None):
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Content-Type": "application/json", **(headers or {})})
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.loads(r.read())


def test_index_meta_state(server):
    app, url = server
    status, body = _get(url)
    assert status == 200 and b"Listen to the Broadcast" in body
    meta = json.loads(_get(url + "api/meta")[1])
    assert len(meta["scales"]) > 40
    state = json.loads(_get(url + "api/state")[1])
    assert state["settings"]["scale"] == app.settings["scale"]


def test_update_settings(server):
    app, url = server
    s = _post(url + "api/settings", {"scale": "saba", "routing": {"dhcp": {"channel": 9}}})
    assert s["scale"] == "saba" and s["routing"]["dhcp"]["channel"] == 9
    assert app.engine.settings["scale"] == "saba"


def test_cross_origin_post_refused(server):
    _, url = server
    with pytest.raises(urllib.error.HTTPError) as err:
        _post(url + "api/settings", {"bpm": 60}, {"Origin": "http://evil.example"})
    assert err.value.code == 403


def test_cc_moves_sliders(server):
    app, _ = server
    app.handle_cc(20, 127)
    assert app.settings["root"] == 11
    app.handle_cc(99, 127)  # unmapped CC is ignored


def test_ports_api_in_dry_run(server):
    _, url = server
    ports = json.loads(_get(url + "api/ports")[1])
    assert ports["dry_run"] and ports["outputs"] == []


def test_reset_keeps_port_choice(server):
    app, url = server
    app.update_settings({"midi_out": "LTB", "bpm": 60})
    s = _post(url + "api/settings/reset", {})
    assert s["midi_out"] == "LTB" and s["bpm"] == 84.0
