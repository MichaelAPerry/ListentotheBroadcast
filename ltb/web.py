"""Local control panel: static page, JSON API and a Server-Sent Events feed (stdlib only)."""

from __future__ import annotations

import json
import queue
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys

from . import settings as settings_mod
from .app import App
from .protocols import PROTOCOLS
from .theory import NOTE_NAMES, SCALES


def static_dir() -> Path:
    # PyInstaller unpacks data files under sys._MEIPASS; otherwise they sit next to this module.
    base = getattr(sys, "_MEIPASS", None)
    return Path(base, "ltb", "static") if base else Path(__file__).with_name("static")


def meta() -> dict:
    return {
        "scales": [{"key": s.key, "name": s.name, "family": s.family, "microtonal": s.microtonal}
                   for s in SCALES],
        "protocols": [{"key": p.key, "label": p.label, "ports": list(p.ports)} for p in PROTOCOLS],
        "roles": list(settings_mod.ROLES),
        "divisions": list(settings_mod.DIVISIONS),
        "progressions": list(settings_mod.PROGRESSIONS),
        "cc_targets": list(settings_mod.CC_TARGETS),
        "note_names": list(NOTE_NAMES),
    }


def make_handler(app: App):
    index_html = static_dir().joinpath("index.html").read_bytes()

    class Handler(BaseHTTPRequestHandler):
        server_version = "ListenToTheBroadcast"

        def log_message(self, fmt, *args):  # keep the console for musical output
            pass

        def _json(self, body, status=HTTPStatus.OK):
            data = json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def _body(self):
            length = int(self.headers.get("Content-Length") or 0)
            if length > 1_000_000:
                raise ValueError("body too large")
            raw = self.rfile.read(length) if length else b"{}"
            body = json.loads(raw or b"{}")
            if not isinstance(body, dict):
                raise ValueError("expected a JSON object")
            return body

        def _host_ok(self) -> bool:
            # Blocks DNS-rebinding: a remote page resolving its own name to 127.0.0.1.
            host = (self.headers.get("Host") or "").rsplit(":", 1)[0]
            if host not in ("127.0.0.1", "localhost", "[::1]"):
                self._json({"error": "bad host"}, HTTPStatus.FORBIDDEN)
                return False
            return True

        def do_GET(self):
            if not self._host_ok():
                return
            path = self.path.split("?", 1)[0]
            if path in ("/", "/index.html"):
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(index_html)))
                self.end_headers()
                self.wfile.write(index_html)
            elif path == "/api/meta":
                self._json(meta())
            elif path == "/api/state":
                self._json(app.state())
            elif path == "/api/ports":
                self._json(app.ports())
            elif path == "/api/feed":
                self._feed()
            else:
                self._json({"error": "not found"}, HTTPStatus.NOT_FOUND)

        def do_POST(self):
            if not self._host_ok():
                return
            path = self.path.split("?", 1)[0]
            # Refuse cross-site form posts: only the panel itself (same origin) may change settings.
            origin = self.headers.get("Origin")
            if origin and origin != f"http://{self.headers.get('Host')}":
                self._json({"error": "cross-origin request refused"}, HTTPStatus.FORBIDDEN)
                return
            try:
                body = self._body()
            except ValueError as e:
                self._json({"error": str(e)}, HTTPStatus.BAD_REQUEST)
                return
            if path == "/api/settings":
                self._json(app.update_settings(body))
            elif path == "/api/settings/reset":
                self._json(app.reset_settings())
            elif path == "/api/ports":
                self._json(app.set_ports(body))
            elif path == "/api/panic":
                app.engine.panic()
                self._json({"ok": True})
            else:
                self._json({"error": "not found"}, HTTPStatus.NOT_FOUND)

        def _feed(self):
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            q = app.hub.subscribe()
            last_state = 0.0
            try:
                while True:
                    try:
                        msg = q.get(timeout=0.25)
                        self.wfile.write(b"data: " + json.dumps(msg).encode() + b"\n\n")
                    except queue.Empty:
                        pass
                    if time.monotonic() - last_state > 0.5:
                        last_state = time.monotonic()
                        state = {"type": "state", **app.state()}
                        state.pop("settings")
                        self.wfile.write(b"data: " + json.dumps(state).encode() + b"\n\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
                pass
            finally:
                app.hub.unsubscribe(q)

    return Handler


class ControlServer:
    def __init__(self, app: App, host: str, port: int):
        self.httpd = ThreadingHTTPServer((host, port), make_handler(app))
        self.httpd.daemon_threads = True
        self._thread = threading.Thread(target=self.httpd.serve_forever, name="ltb-web", daemon=True)

    @property
    def url(self) -> str:
        host, port = self.httpd.server_address[:2]
        return f"http://{host}:{port}/"

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self.httpd.shutdown()
        self.httpd.server_close()
