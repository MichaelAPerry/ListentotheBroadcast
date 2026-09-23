"""Command line entry point: ``python -m ltb``."""

from __future__ import annotations

import argparse
import logging
import signal
import threading
import webbrowser
from pathlib import Path

import mido

from .app import App
from .listeners import BroadcastListener
from .midi_io import ClockFollower, DryRunOut, InternalClock, VIRTUAL_PORT_NAME, open_output
from .simulate import Simulator
from .web import ControlServer


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(prog="ltb", description="Listen to the Broadcast: LAN broadcast traffic -> MIDI")
    p.add_argument("--out", help="MIDI output port name (substring ok). Omit on macOS/Linux to create a virtual port")
    p.add_argument("--clock-in", help="MIDI input port for host clock/transport and CC control (substring ok)")
    p.add_argument("--list-ports", action="store_true", help="list MIDI ports and exit")
    p.add_argument("--dry-run", action="store_true", help="don't open MIDI; print notes instead")
    p.add_argument("--simulate", action="store_true", help="use synthetic LAN traffic instead of listening")
    p.add_argument("--interface", default="0.0.0.0", help="local IPv4 address of the NIC to join multicast on")
    p.add_argument("--settings", default="ltb-settings.json", help="settings file (auto-saved)")
    p.add_argument("--http-port", type=int, default=8765, help="control panel port (localhost only)")
    p.add_argument("--no-browser", action="store_true", help="don't open the control panel automatically")
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args(argv)

    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING, format="%(levelname)s %(message)s")

    if args.list_ports:
        print("MIDI outputs:", *mido.get_output_names() or ["(none)"], sep="\n  ")
        print("MIDI inputs:", *mido.get_input_names() or ["(none)"], sep="\n  ")
        return

    if args.dry_run:
        out, out_name = DryRunOut(verbose=True), "dry run (console)"
    else:
        out = open_output(args.out)
        out_name = getattr(out, "name", None) or args.out or VIRTUAL_PORT_NAME

    app = App(out, Path(args.settings) if args.settings else None, out_name)

    if args.clock_in:
        app.clock_follower = ClockFollower(app.engine, args.clock_in, app.handle_cc)

    if args.simulate:
        source = Simulator(app.engine.submit)
        app.simulating = True
    else:
        source = BroadcastListener(app.engine.submit, interface=args.interface)
        app.listener = source
    source.start()

    clock = InternalClock(app.engine)
    clock.start()

    server = ControlServer(app, "127.0.0.1", args.http_port)
    server.start()

    print(f"Listen to the Broadcast -> MIDI out: {out_name}")
    if app.listener:
        for name, status in sorted(app.listener.status.items()):
            print(f"  listening {name:<16} {status}")
    else:
        print("  simulating LAN traffic")
    print(f"Control panel: {server.url}   (Ctrl+C to quit)")
    if not args.no_browser:
        webbrowser.open(server.url)

    done = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: done.set())
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, lambda *_: done.set())
    while not done.wait(0.5):
        pass

    print("\nStopping: releasing all notes")
    clock.stop()
    source.stop()
    app.engine.panic()
    server.stop()
    if app.clock_follower:
        app.clock_follower.close()
    app.close()
    out.close()


if __name__ == "__main__":
    main()
