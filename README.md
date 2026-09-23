# Listen to the Broadcast

A VST3 instrument that turns your local network's broadcast chatter into ambient music.

Your network is constantly talking to itself:
- AirPlay and Chromecast announcing themselves,
- printers saying hello,
- Windows machines looking up names,
- Spotify and Dropbox beaconing,
- phones joining Wi-Fi.

Load **Listen to the Broadcast** in VSTHost or any DAW and it plays that traffic as notes, chords and pads, on the beat and in the scale you choose. It has its own built-in sounds, so you hear it straight away. It can also send MIDI to drive your other instruments.

- **Everything happens inside the plugin.** No helper app, no browser, no loopMIDI.
- **No admin rights, no packet capture.** It uses plain UDP sockets that share ports with Windows' own discovery services. It only listens and never sends anything.
- **On the beat.** It follows your host's tempo and transport. When the host is stopped (VSTHost often is), it keeps playing on its own clock.
- **About 50 scales:**
  - Western modes, pentatonics, blues, whole tone, diminished
  - Middle Eastern: Hijaz, Double Harmonic, Nikriz, Persian…
  - **True quarter-tone Arabic maqamat:** Rast, Bayati, Saba, Sikah
  - Hindustani thaats, Japanese and Chinese pentatonics, Pelog, Slendro
- **Chord controls:** degree, size, stacking (thirds, fourths, fifths, clusters), voicing spread, progression, and when chords change (every N bars, or when a new device joins the network).
- **One row per traffic type.** Each row sets on/off, role (note, chord, pad, bass, hit), built-in sound, MIDI channel, octave, rhythm, length, velocity, notes per bar and chance, plus a ▶ button to test it.
- **Every device has its own note.** Each device on your network gets a stable scale degree, so you learn to hear your printer.
- **Nothing lingers.** Remove the plugin or close the host and it stops. **Panic** stops every note instantly.

## Install (Windows)

1. Download `ListenToTheBroadcast-VST3-Windows.zip` from the latest successful **build** run in this repo's *Actions* tab.
2. Copy the `Listen to the Broadcast.vst3` folder into `C:\Program Files\Common Files\VST3\`.
3. In VSTHost (a version with VST3 support) or your DAW, rescan plugins and load **Listen to the Broadcast** as an instrument.
4. When Windows Firewall asks whether the host may receive network traffic, allow **private** networks.
5. Press **Test sound**.

## The window

| Area | What's there |
|---|---|
| Header | Bar and beat, current chord, tempo (host or free-run), devices heard, notes sounding. **Test sound** and **Panic** buttons. |
| Sound & Beat | Built-in sound on/off, MIDI out on/off, level, reverb, sync, tempo, swing, notes per step, and how MIDI handles microtones. |
| Harmony | Root, scale (grouped by tradition; ¼ marks quarter-tone scales), octave, chord degree, size, stacking, spread, progression, and chord-change timing. |
| Live traffic | Packets as they arrive, and which ports are open. |
| Traffic → Sound | One row per traffic type, with an activity meter, packet and note counts, settings, and ▶ to test that row. |

Every control is a normal plugin parameter. Your host saves them with the project and can automate them.

### Default sounds

| Traffic | Role | Sound | MIDI ch |
|---|---|---|---|
| mDNS / Bonjour | note (each device its own degree) | Bell | 1 |
| SSDP / UPnP | pad (breathes while there's traffic) | Pad | 2 |
| LLMNR | note | Glass | 3 |
| WS-Discovery | note | Pluck | 4 |
| DHCP (device joins) | chord | Bell | 5 |
| NetBIOS | bass | Bass | 6 |
| LAN sync (Dropbox/Spotify/Steam) | hit | Hat | 10 |

### Using your own instruments

Leave **MIDI out** on and route the plugin's MIDI output to other instruments in your host. Each traffic type plays on its own channel. Set a row's sound to **MIDI only** to silence the built-in voice for that row.

For quarter-tone scales, MIDI out bends the channel's pitch on note and bass rows, so set your synth's pitch-bend range to match **Bend range**. Chords round to the nearest semitone over MIDI. The built-in sound always plays microtones exactly.

## How it works

```
UDP sockets on a background thread (mDNS 5353, SSDP 1900, LLMNR 5355, WSD 3702,
DHCP 67/68, NetBIOS 137/138, Dropbox/Spotify/Steam 17500/57621/27036),
multicast joined on every network adapter
   → lock-free queue (never blocks the audio thread; drops when full)
   → audio thread: 16th-note grid from the host's position (or free-run clock), swing
   → harmony: scale + progression → chord; device address → stable scale degree
   → roles → notes → built-in synth + reverb, and MIDI out
```

The design reasoning and prior art are in [docs/architecture-evaluation.md](docs/architecture-evaluation.md).

## Building

You need CMake 3.22+ and a C++17 compiler (Visual Studio 2022 on Windows). CMake downloads JUCE 8 on first configure.

```sh
cmake -S . -B build
cmake --build build --config Release --target ListenToTheBroadcast_VST3
```

The plugin lands in `build/ListenToTheBroadcast_artefacts/Release/VST3/`.

Tests:

```sh
cmake --build build --config Release --target ltb_core_tests ltb_plugin_tests
build/ltb_core_tests              # engine, scales, synth, listener (Windows: build\Release\)
build/ltb_plugin_tests_artefacts/Release/ltb_plugin_tests screenshot.png
```

CI (`.github/workflows/build.yml`) builds on Windows and Linux. On both it runs the tests and validates the plugin with [pluginval](https://github.com/Tracktion/pluginval) at strictness 10. The Windows job uploads the plugin zip.

Built with [JUCE](https://juce.com) (AGPLv3). This project is GPLv3.
