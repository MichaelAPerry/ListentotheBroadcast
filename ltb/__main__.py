"""Entry point: ``python -m ltb``, the ``ltb`` script, or the bundled Windows app."""

from __future__ import annotations

import argparse
import logging
import os
import signal
import sys
import threading
import webbrowser
from pathlib import Path

from .app import App
from .listeners import BroadcastListener
from .midi_io import DryRunOut, InternalClock, SwitchableOut, input_names, output_names
from .simulate import Simulator
from .web import ControlServer
from .window import run_window


def default_settings_path() -> Path:
    if sys.platform == "win32":
        base = Path(os.environ.get("APPDATA") or Path.home())
    elif sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support"
    else:
        base = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
    return base / "ListenToTheBroadcast" / "settings.json"


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(prog="ltb", description="Listen to the Broadcast: LAN broadcast traffic -> MIDI")
    p.add_argument("--out", help="MIDI output port (substring ok). Default: last used, else auto-detect")
    p.add_argument("--clock-in", help="MIDI input for host clock/transport and CC control (substring ok)")
    p.add_argument("--list-ports", action="store_true", help="list MIDI ports and exit")
    p.add_argument("--dry-run", action="store_true", help="don't open MIDI; print notes instead")
    p.add_argument("--simulate", action="store_true", help="use synthetic LAN traffic instead of listening")
    p.add_argument("--interface", default="0.0.0.0", help="local IPv4 address of the NIC to join multicast on")
    p.add_argument("--settings", type=Path, default=None, help="settings file (auto-saved)")
    p.add_argument("--http-port", type=int, default=8765, help="control panel port (localhost only)")
    ui = p.add_mutually_exclusive_group()
    ui.add_argument("--browser", action="store_true", help="open the panel in a browser tab instead of a window")
    ui.add_argument("--headless", "--no-browser", action="store_true", help="no window; panel stays reachable by URL")
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args(argv)

    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING, format="%(levelname)s %(message)s")

    if args.list_ports:
        print("MIDI outputs:", *output_names() or ["(none)"], sep="\n  ")
        print("MIDI inputs:", *input_names() or ["(none)"], sep="\n  ")
        return

    settings_path = args.settings or default_settings_path()
    settings_path.parent.mkdir(parents=True, exist_ok=True)
    out = DryRunOut(verbose=args.headless) if args.dry_run else SwitchableOut()
    app = App(out, settings_path)
    port_error = app.connect_output(args.out)
    clock_wanted = args.clock_in if args.clock_in is not None else app.settings["clock_in"]
    if clock_wanted:
        port_error = app.connect_clock(clock_wanted) or port_error

    if args.simulate:
        source = Simulator(app.engine.submit)
        app.simulating = True
    else:
        source = BroadcastListener(app.engine.submit, interface=args.interface)
        app.listener = source
    source.start()

    clock = InternalClock(app.engine)
    clock.start()

    try:
        server = ControlServer(app, "127.0.0.1", args.http_port)
    except OSError:  # port in use (e.g. a second copy running): take any free port
        server = ControlServer(app, "127.0.0.1", 0)
    server.start()

    print(f"Listen to the Broadcast -> MIDI out: {app.out_name or '(none)'}")
    if port_error:
        print("  " + port_error)
    if app.listener:
        for name, status in sorted(app.listener.status.items()):
            print(f"  listening {name:<16} {status}")
    else:
        print("  simulating LAN traffic")
    print(f"Control panel: {server.url}")

    done = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: done.set())
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, lambda *_: done.set())

    if not args.headless and not args.browser and run_window(server.url):
        pass  # window closed by the user
    else:
        if not args.headless:
            webbrowser.open(server.url)
        print("Ctrl+C to quit")
        while not done.wait(0.5):
            pass

    print("\nStopping: releasing all notes")
    clock.stop()
    source.stop()
    app.engine.panic()
    server.stop()
    app.close()
    out.close()


if __name__ == "__main__":
    main()
