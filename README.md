# Listen to the Broadcast

Your local network is constantly talking to itself:
- AirPlay and Chromecast announcing themselves,
- printers saying hello,
- Windows machines looking up names,
- Spotify and Dropbox beaconing,
- phones joining Wi-Fi.

This tool listens to that broadcast chatter and turns it into beat-synced, scale-aware MIDI that you can play through any DAW or plugin host (VSTHost, Reaper, Ableton, Bitwig, FL Studio, Cubase, and so on).

- **No admin rights, no packet capture.** It uses plain UDP sockets that share the ports with your OS's own discovery services. No Npcap/libpcap and no ARP.
- **On the beat.** Packets are held until the next grid slot (1/16 up to 1 bar). Timing comes from the built-in clock or from your host's MIDI clock.
- **Scales from many traditions.** About 50 scales: Western modes, Middle Eastern (Hijaz, Nikriz, Persian…), **true quarter-tone Arabic maqamat** (Rast, Bayati, Saba, Sikah, played with pitch bend), Hindustani thaats, Japanese/Chinese pentatonics, and Pelog/Slendro.
- **Sliders for chords.** Chord degree, size, stacking (thirds, fourths, fifths or clusters), voicing spread, progression, and when chords change (every N bars, or when a new device joins the network).
- **Each traffic type gets its own sound.** Every type has its own MIDI channel, program, role (note/chord/pad/bass/hit), rhythm, length, velocity, density cap and chance.
- **Every device has its own note.** Each device on your network gets a stable scale degree, so you learn to hear your printer.

## Quick start (Windows app, no Python needed)

1. **One-time:** install [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) and create a port named `LTB`. If you're on Windows 11 with Windows MIDI Services, its built-in *loopback* ports work too; skip loopMIDI.
2. Download `ListenToTheBroadcast-windows.zip` from the latest successful **build** run under this repo's *Actions* tab. Unzip it anywhere and double-click `ListenToTheBroadcast.exe`.
   - The app is unsigned, so Windows SmartScreen will warn you: choose *More info → Run anyway*.
   - Windows Firewall will ask whether it may receive network traffic: allow **private** networks.
3. The control window opens and picks the `LTB` port automatically. You can change the port in the **MIDI** box, and the app remembers your choice.
4. In VSTHost (or any host), set the MIDI input to `LTB`, then load one instrument per channel (see below).

Settings save automatically to `%APPDATA%\ListenToTheBroadcast\settings.json`.

### From source (any OS)

```sh
pip install -e .          # mido, python-rtmidi, pywebview (Windows/macOS)
python -m ltb --simulate  # fake traffic, to hear it straight away
python -m ltb             # the real network
```

Other useful flags:
- `--browser` shows the panel in a browser tab instead of a window.
- `--headless` shows no UI; the panel is still at <http://127.0.0.1:8765/>.
- `--list-ports` lists MIDI ports.
- `--dry-run` prints notes instead of sending MIDI.
- `--interface 192.168.1.20` picks which network card to listen on.

On macOS and Linux, the app offers its own virtual MIDI port ("Listen to the Broadcast"), so no loopMIDI is needed. On Linux, the window needs `pip install pywebview[qt]`; otherwise the panel opens in your browser.

Some ports may show as "unavailable" in the panel's listener list, typically NetBIOS on Windows, or ports below 1024 on Linux without privileges. That's fine: every other traffic type keeps working.

## Using it with VSTHost (or any host)

1. In the host, choose the `LTB` port as a **MIDI input**.
2. Load one instrument per traffic type and set each instrument's **MIDI channel filter** to match the channel in the *Traffic → Sound* table. The defaults are:

   | Channel | Traffic type | Role | Good sound |
   |---|---|---|---|
   | 1 | mDNS / Bonjour | note | bells, kalimba, plucks |
   | 2 | SSDP / UPnP | pad | a slow pad that swells with traffic |
   | 3 | LLMNR | note | a high glassy melody |
   | 4 | WS-Discovery | note | sparse chimes |
   | 5 | DHCP (device joins) | chord | a big evolving chord |
   | 6 | NetBIOS | bass | sub bass |
   | 10 | LAN sync beacons | hit | drums or percussion, fixed note 42 |

3. With a multi-timbral or General MIDI synth, you can put everything on one instance and use the **Program** column to choose each patch.
4. Add reverb and delay generously. This is ambient music.

### Beat sync with the host

- **Internal (default):** set *Tempo* in the panel to match the host.
- **Host MIDI clock:** first create a *second* loopMIDI port (e.g. `LTB Clock`) and have the host send MIDI clock (and Start/Stop) to it. Then pick that port as **Clock / CC in** in the app's MIDI box and set *Sync* to **Host MIDI clock**. After that, the host's play/stop and tempo drive everything, and pressing Play restarts at bar 1. The header shows the tempo it's receiving.

### Controlling the sliders from the host

Send MIDI CC on the **Clock / CC in** port to move the panel's sliders. That lets you automate them from host automation lanes or a hardware controller:

| CC | Slider |
|---|---|
| 20 | root |
| 21 | scale |
| 22 | chord degree |
| 23 | chord size |
| 24 | chord stacking |
| 25 | tempo |
| 26 | master level |
| 27 | swing |

### Microtones

Maqam scales (marked ¼ in the panel) are played exactly using pitch bend on the *note* and *bass* roles. Set each synth's pitch-bend range to match *Bend range* in the panel (default ±2 semitones). Chords and pads round to the nearest semitone, because one pitch-bend per channel can't retune the notes of a chord independently.

## How it works

```
UDP sockets (mDNS 5353, SSDP 1900, LLMNR 5355, WSD 3702, DHCP 67/68, NetBIOS 137/138, 17500/57621/27036)
   → per-type lossy queues  (never block; excess is dropped)
   → 24-PPQN clock (internal or host MIDI clock) → 16th-note grid, swing
   → harmony: scale + progression → chord; device IP → stable scale degree
   → roles → MIDI notes, pitch bend, program change, traffic-intensity CC
   → virtual MIDI port → host
```

Everything runs outside the DAW, so a crash or network hiccup can't glitch your audio. On exit (Ctrl+C) and on the **Panic** button, every note is released and All Notes Off is sent on all 16 channels.

The design reasoning and prior art are in [docs/architecture-evaluation.md](docs/architecture-evaluation.md).

## Development

```sh
pip install -e ".[dev]"
pytest
pip install pyinstaller && pyinstaller packaging/ltb.spec --noconfirm   # app bundle in dist/
```

CI (`.github/workflows/build.yml`) runs the tests on Windows, macOS and Linux. It then builds the Windows app and smoke-tests it: the window must open and the clock must advance. The zip is uploaded as a workflow artifact.
