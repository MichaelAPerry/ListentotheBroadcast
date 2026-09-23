# Listen to the Broadcast: Architecture Evaluation

Goal: turn live **local-network broadcast and multicast traffic** into ambient music and notes inside a DAW, without ever putting the DAW's audio thread at risk.

---

## 0. First principles (these apply to every method below)

### 0.1 What traffic is actually heard

On a modern switched LAN or Wi-Fi network, a normal NIC only sees three kinds of traffic: broadcast, multicast, and your own unicast. That limits what we can hear, and it's also why the idea works. The "broadcast layer" of a home or office network is a steady, low-rate, device-specific chatter:

| Source | Transport | Port / Type | Musical character |
|---|---|---|---|
| ARP who-has / gratuitous ARP | L2 broadcast | EtherType 0x0806 | Heartbeat. Regular, percussive |
| DHCP discover/request | UDP broadcast | 67/68 | Rare events: a device joins. Good for phrase or chord changes |
| mDNS / Bonjour | UDP multicast 224.0.0.251 | 5353 | Rich service names (`_airplay`, `_googlecast`, `_spotify-connect`). Timbre selection |
| SSDP / UPnP | UDP multicast 239.255.255.250 | 1900 | Bursty NOTIFY storms. Swells and pads |
| LLMNR / NetBIOS-NS | multicast / broadcast | 5355 / 137 | Windows machines talking. Separate voice |
| WS-Discovery | multicast | 3702 | Printers and cameras. Sparse bells |
| Vendor LAN sync (Dropbox, Spotify, Steam, etc.) | UDP broadcast | 17500, 57621, 27036... | Rhythmic periodic beacons. Ostinati |

Typical rates run from tens to a few hundred packets per second. That rate drives most of the engineering decisions below: **raw throughput is not the problem; privileges, timing and robustness are.**

### 0.2 You mostly don't need packet capture

Everything above except ARP (and the rest of L2) is **UDP to a well-known port**. A normal, unprivileged process can receive it with an ordinary socket:

- bind with `SO_REUSEADDR` (plus `SO_REUSEPORT` on macOS/Linux) so the socket coexists with `mDNSResponder`/`avahi`/SSDP services,
- `IP_ADD_MEMBERSHIP` for 224.0.0.251, 239.255.255.250 and so on,
- enable `SO_BROADCAST` for the broadcast ports.

This avoids root, libpcap/Npcap, and BPF permission prompts. Caveats:
- Ports below 1024 (67/68/137/138) need `CAP_NET_BIND_SERVICE` on Linux.
- ARP requires real capture.

The recommended design is therefore **two ingress tiers**: an unprivileged socket tier that always works, plus an optional privileged capture tier that adds ARP and other L2 detail.

### 0.3 Real-time safety rules for the DAW

1. **Nothing network-related ever runs on the audio thread.** No sockets, no locks, no allocation, no logging.
2. **Process isolation beats thread isolation.** A sniffer crash, hang or permission failure must not be able to take down the DAW. Ideally it can't even glitch the DAW.
3. **Map features, not packets.** One note per packet produces unmusical note floods during an SSDP storm. Aggregate into windows (rates, inter-arrival statistics, entropy, "new device" events) and derive notes from those.
4. **Hard density caps plus guaranteed note-offs.** Rate-limit notes per voice, track active notes, and send All-Notes-Off on shutdown and on reconnect. Stuck notes are the most common failure in network-driven MIDI.
5. **Sync to the DAW clock, not wall clock.** Quantize to Ableton Link or incoming MIDI clock so network jitter becomes musical placement instead of timing error.
6. **Privacy by default.** Hash MACs/IPs into stable identifiers (for example, a salted hash mapped to a scale degree). Never log payloads.

---

## 1. Evaluation of the three proposed methods

### Method A: Standalone Python (Scapy) to Virtual MIDI / OSC

```
[NIC] → Python process (Scapy AsyncSniffer and/or plain UDP sockets)
       → feature aggregator → music engine (quantized via Link / MIDI clock)
       → python-rtmidi virtual port  ──► DAW MIDI input
       → (optional) python-osc UDP   ──► DAW OSC (Reaper/Bitwig) or M4L
```

**Pros**
- Fastest iteration loop: edit a mapping, restart in under a second, and the DAW never reloads.
- Full process isolation. The worst case is that the music stops.
- Works with every DAW, because virtual MIDI is the universal interface.
- Rich ecosystem: `scapy`, `mido`/`python-rtmidi`, `python-osc`, `aalink`/LinkPython for Ableton Link, `numpy` for feature windows.
- No C++ toolchain, no code signing, no plugin validation (auval/pluginval).

**Cons**
- **Capture privileges.** Scapy needs root/`sudo` on macOS and Linux, and Npcap on Windows. You can mostly remove this with the unprivileged socket tier (§0.2).
- **Virtual MIDI port availability varies by OS:**
  - macOS: the built-in IAC Driver works, or python-rtmidi can create its own virtual port.
  - Linux: ALSA virtual ports work natively.
  - Windows: rtmidi can't create virtual ports. You need loopMIDI, or the loopback endpoints in Windows MIDI Services on current Windows 11.
- Timing: MIDI goes out in real time with no timestamps, so jitter is about 1–5 ms from the Python scheduler and GIL. That's inaudible for ambient pads, and quantizing to Link hides it.
- Scapy dissection in pure Python is slow, roughly a few thousand pps. That's fine for broadcast rates, but use a tight BPF filter (`arp or broadcast or multicast`) so it doesn't choke on unicast bursts.
- OSC-to-DAW is uneven: Reaper has native OSC, Bitwig uses controller scripts, and Ableton needs M4L. **Prefer MIDI as the primary output and treat OSC as optional.**

| | Rating |
|---|---|
| Stability | ★★★★★ (isolated process; DAW-safe by construction) |
| Setup friction | ★★★★☆ (low; one `pip install`. Friction is capture privileges plus loopMIDI on Windows) |
| Timing precision | ★★★☆☆ (ms-level, fine for ambient) |
| Portability across DAWs | ★★★★★ |

### Method B: Max for Live device with native sockets

```
[NIC] → (UDP only) [udpreceive]  or  [node.script] (Node dgram, multicast-capable)
       → Max patch: smoothing / scales / live.* params
       → MIDI out of the device on an Ableton MIDI track
```

**Pros**
- Deepest Ableton integration:
  - transport-locked scheduling (`transport`, `metro @quantize`),
  - automatable, mappable `live.*` parameters saved with the Set,
  - Live API access to launch clips, change scenes and modulate any device.
- Visual patching suits experimenting with mappings.
- Node for Max runs JavaScript in a **separate Node process**. It has `dgram`, which supports broadcast and multicast membership, so the unprivileged socket tier (§0.2) ports over cleanly.

**Cons**
- **No real packet capture.** `[udpreceive]` parses OSC-formatted messages only; it's not a raw UDP or L2 reader. It also has no documented multicast group join, so you'd go through `node.script` instead. ARP and L2 need a native pcap addon (for example, the `cap` npm module) compiled against Node for Max's bundled Node ABI. That's fragile and breaks when Max updates.
- Ableton-only lock-in, and it requires Live Suite (or the M4L add-on).
- Max's scheduler is shared with the Live set. Heavy JavaScript in `[js]` (not `node.script`) runs on the low-priority thread and can stall UI or MIDI timing. Keep the parsing in `node.script` and send only compact events into the patch.
- Distributing it means `.amxd` freezing, Node module bundling and per-OS native binaries.

| | Rating |
|---|---|
| Stability | ★★★★☆ (good if parsing stays in `node.script`; a bad `[js]` loop can hang the Set) |
| Setup friction | ★★★☆☆ (none for Suite owners; high if you need pcap/ARP) |
| Timing precision | ★★★★☆ (transport-locked, sample-accurate relative to Live's clock) |
| Portability across DAWs | ★☆☆☆☆ |

### Method C: JUCE VST3/AU/CLAP plugin plus privileged helper process

```
[NIC] → helper daemon (libpcap/Npcap, root or launchd/SMAppService/Windows service)
       → IPC (localhost UDP, Unix socket/named pipe, or shared-memory ring)
       → plugin: non-RT reader thread → lock-free SPSC FIFO (juce::AbstractFifo)
       → processBlock(): drain FIFO → MidiBuffer at sample offsets (and/or internal synth)
```

**Pros**
- Sample-accurate event placement inside `processBlock`, using host transport and PPQ position directly.
- Lives inside the session. It saves with the project and has automatable parameters, presets and a GUI. It can also be a **synth** that renders audio itself, which removes the virtual-MIDI dependency entirely.
- The helper is shared across plugin instances and DAWs. The sniffer is written once, and multiple instances subscribe.
- Commercial-grade distribution if the project goes that way.

**Cons**
- **Highest toolchain friction:**
  - CMake, plus Xcode or MSVC.
  - JUCE licensing: AGPLv3 or commercial, which is compatible with this repo's GPLv3 via AGPL §13, but worth confirming. The VST3 SDK has been MIT-licensed since 3.8.
  - Code signing and notarization.
  - Validating with `pluginval` and `auval`.
- **Two install surfaces.** The helper needs elevated privileges and an installer (macOS `SMAppService` daemon, a Windows service with Npcap, a systemd unit). That's where most support pain lives.
- Sandbox and host quirks:
  - AUv3/sandboxed hosts need network entitlements to reach the helper.
  - Some hosts run plugins out-of-process (Logic, Bitwig sandboxing).
  - MIDI output from plugins is inconsistent. Ableton, for example, can route a VST's MIDI out to another track but has no MIDI-effect VST slot.
- A plugin bug (such as a lock or allocation on the audio thread) **directly glitches or crashes the DAW**. The blast radius is much larger than in A or B.

| | Rating |
|---|---|
| Stability | ★★★☆☆ (excellent if the SPSC discipline is perfect; catastrophic if not) |
| Setup friction | ★☆☆☆☆ (developer and end user both pay) |
| Timing precision | ★★★★★ (sample-accurate) |
| Portability across DAWs | ★★★★☆ (format and host MIDI-routing quirks) |

### Summary matrix

| Criterion | A: Python → vMIDI | B: Max for Live | C: JUCE + helper |
|---|---|---|---|
| DAW crash risk | None (separate process) | Low (Node separate; `[js]` risk) | **Medium–High** (in-process) |
| Raw L2 / ARP capture | Yes (root/Npcap) | Painful (native Node addon) | Yes (helper) |
| Unprivileged UDP tier | Yes | Yes (`node.script`) | Yes (helper or plugin thread) |
| Timing | ~1–5 ms jitter | Transport-locked | Sample-accurate |
| DAW support | All | Ableton Suite only | All plugin hosts |
| Toolchain | Python | Max (bundled) | C++/CMake/SDKs/signing |
| Time to first sound | **Hours** | Hours–day | Days–weeks |

**Recommendation:** start with **Method A**, built as the two-tier ingress (§0.2) and synced to Ableton Link or MIDI clock. It gives the most musical exploration for the least risk. Keep the capture-and-features core as a separate module that emits a small, versioned event schema (see §3), so it can later feed a plugin (Method C, or §3.3) without a rewrite.

---

## 2. Existing open-source work (prior art)

| Project | What it does | Relevance |
|---|---|---|
| [jweir/Sound-of-Traffic](https://github.com/jweir/Sound-of-Traffic) | Converts network traffic to MIDI. Ports map to instruments, src/dst to odd/even ticks, note from per-port hit count | Closest to "packet → MIDI → DAW". Its tick-based quantization is a good idea to borrow |
| [gaddman/sonnet](https://github.com/gaddman/sonnet) | Python + TShark + pygame MIDI. Maps any Wireshark field to notes via configurable mappings (protocol / ip / tcp presets) | Using TShark as the dissector avoids Scapy's speed limits, and the declarative field→note mapping is worth copying |
| [dmeldrum6/Network-Sonification](https://github.com/dmeldrum6/Network-Sonification) | Real-time capture with visual and audio output. Packet size maps to volume and pitch | Reference for a combined visualizer and sonifier |
| [wesleygoatley/listener](https://github.com/wesleygoatley/listener) | Raspberry Pi + Pure Data sonifying Wi-Fi **probe requests** | Artistic precedent for sonifying the *broadcast* layer specifically; installation-style |
| [Soniweb](https://jackson.gd/soniweb/) ([MCT blog](https://mct-master.github.io/sonification/2020/03/09/Soniweb.html)) | Script plus patch that spatializes web traffic in 5th-order ambisonics as bells | Ambient aesthetic, spatial mapping idea (geo/IP → position) |
| [dj-arp-storm (PyPI)](https://pypi.org/project/dj-arp-storm/1.0b6) | Plays network traffic, notably ARP, as sound | Directly on-topic for ARP broadcast |
| [Peep: The Network Auralizer](https://sourceforge.net/projects/peep/) ([USENIX LISA 2000](https://www.usenix.org/conference/lisa-2000/peep-network-auralizer-monitoring-your-network-sound), [GitHub mirror](https://github.com/rubicks/peep)) | Classic GPL system: network events → "sonic ecology" of natural sounds | Best conceptual prior art for **ambient** (not melodic) monitoring: "normal network" sounds like a calm forest |
| [SoNSTAR](https://paulvickers.github.io/SoNSTAR/) ([PLOS One paper](https://journals.plos.org/plosone/article?id=10.1371%2Fjournal.pone.0195948)) | Research system: flow features → MIDI → synthesizers as a soundscape for situational awareness | Validates the **feature-based (flow) mapping** approach over per-packet notes |
| [SOCS / self-organized criticality sonification (arXiv)](https://arxiv.org/pdf/1407.4705) | Python capture → Pure Data, using log-returns of traffic variables | Shows a good normalization trick: sonify *changes*, not absolute levels |
| [Sonification aesthetics for network SA (arXiv)](https://arxiv.org/pdf/1409.5282) | Design study on pleasant, long-duration network sonification | Guidance on listener fatigue, which matters for ambient |
| [carthach/ableson](https://github.com/carthach/ableson) | Max for Live tools for time-series sonification | Starting point for Method B's device UI |
| [Provokke/tether-m4l](https://github.com/Provokke/tether-m4l) | M4L + Node for Max WebSocket bridge to Live | Reference implementation of the `node.script` socket pattern |
| [Manifest Audio Sonification Bundle](https://manifest.audio/sonification-bundle) | Commercial M4L data-to-MIDI / data-to-mod devices that accept OSC | Off-the-shelf "OSC in → musical MIDI" backend for a Method A sender |
| [interactive-sonification/sc3nb](https://github.com/interactive-sonification/sc3nb) | Python ↔ SuperCollider for sonification | Alternative synthesis backend (skip the DAW, or send audio into it) |
| [gbevin/SendMIDI](https://github.com/gbevin/SendMIDI) | Cross-platform CLI to send MIDI messages | Enables the zero-code shell pipeline in §3.4 |
| [sonifydata/twotone](https://github.com/sonifydata/twotone) | Web data-to-music app with live MIDI output | Precedent for the browser bridge in §3.1 |

**Gap in the landscape.** None of these combine (a) a focus on the *broadcast/multicast* layer, (b) unprivileged capture, (c) DAW-clock-synced quantization, and (d) feature-based ambient mapping with density control. That combination is this project's niche.

---

## 3. Alternative architectures (lower friction, still real-time safe)

Common thread: split **sensing** (privileged, messy, crashy) from **playing** (real-time, must never block). Connect them with a small, versioned, **lossy-by-design** event stream. Dropping events is fine; blocking is not.

Suggested event schema (JSON over WebSocket/UDP, or packed binary for shared memory):

```json
{"v":1,"t":1732563123.512,"kind":"mdns","dev":"a3f1","svc":"_airplay._tcp","rate":4.2,"new":false}
```

### 3.1 Browser bridge: WebSocket → Web MIDI with scheduled timestamps

```
sensor (Python/Node, unprivileged socket tier) ──WebSocket──► localhost web page
page: mapping UI + JS music engine
      ├─ Web MIDI  output.send(bytes, performance.now() + lookahead) ──► IAC/loopMIDI ──► DAW
      └─ or WebAudio synth ──► system audio / loopback device ──► DAW audio input
```

- **Why it's low friction:** no installs beyond a browser. The mapping is live-editable JavaScript with instant reload, and you get visualization (canvas/WebGL) for free.
- **Why it's real-time safe:** the browser is a separate process. Web MIDI's `send()` takes a **timestamp**, so the page schedules notes against `performance.now()` with a lookahead (say 50–100 ms). WebSocket jitter disappears into the scheduling window, the same lookahead-scheduler pattern Web Audio sequencers use. Output timing becomes *more* stable than Method A's immediate sends.
- **Caveats:** Web MIDI is Chromium/Firefox-only (not Safari). You still need a virtual MIDI port. The Web MIDI clock (`performance.now`) differs from the `AudioContext` clock, so pick one.
- **Bonus:** the sensor can run on a Raspberry Pi elsewhere on the LAN (put it on the segment you want to "listen" to) and serve the page. The DAW machine only needs a browser.

### 3.2 Control-as-audio: "CV over a virtual audio cable"

```
sensor → tiny audio renderer (Python sounddevice / Rust cpal)
       → writes slow control signals (0..1 envelopes, gates, trigger clicks) as audio channels
       → BlackHole / VB-Cable / PipeWire loopback (multi-channel)
       → DAW audio input tracks → CV tools / envelope followers / sidechains / audio-to-MIDI
```

- **Unusual angle:** the DAW's *audio input path* is the most battle-tested, real-time-safe ingress it has. Every DAW reads audio inputs on the RT thread without locks, by design. Encoding network features as audio-rate control signals uses that path directly.
- Each channel is a modulation lane: channel 1 = ARP rate envelope, channel 2 = mDNS triggers, channel 3 = "new device" gate, and so on. In Ableton, CV Tools and Envelope Follower map these to anything. Bitwig's modular grid and Reaper's parameter modulation ("audio control signal") do the same natively.
- You can also send the ambient texture itself. Render drones in the sensor process and let the DAW handle only effects and mixing.
- **Pros:** no MIDI ports, no plugins, no toolchain. You get sample-clocked continuous modulation (not 7-bit CC steps), and it works across DAWs.
- **Cons:** needs a virtual audio driver install. Latency is one device buffer (fine for ambient). Clock drift between two audio devices needs an aggregate device on macOS, or the DAW's own resampling.

### 3.3 Shared-memory ring buffer + tiny Rust plugin (nih-plug, CLAP/VST3)

```
sensor (any language) ──writes──► mmap'd file / POSIX shm: fixed-size SPSC ring of 32-byte events
plugin (Rust, nih-plug) process(): wait-free read of ring → NoteEvents at sample offsets
```

- **Why this beats Method C on friction:**
  - The plugin has **no sockets, no threads, and no IPC library**. It maps a file once in `initialize()`, and after that the audio thread does only atomic loads and memcpy, with no syscalls.
  - `nih-plug` plus `cargo xtask bundle` produces VST3 and CLAP from one `cargo` toolchain, with no JUCE, CMake or Xcode project juggling.
- **Why it's the most real-time safe of the in-DAW options:**
  - The ring is lossy (the writer overwrites and the reader tracks a sequence number), so a dead or slow sensor never blocks the plugin.
  - A stale-heartbeat field lets the plugin fade out gracefully if the sensor dies.
- The sensor stays the privileged part and can remain the same Python code from Method A, writing into the ring with `mmap` + `struct`.
- **Caveats:** sandboxed or out-of-process hosts may restrict shm paths, so use a user-writable temp path with a well-known name. Windows uses a named file mapping instead of POSIX shm.

### 3.4 (Quick-start hack) Unix pipeline: `tcpdump | awk | sendmidi`

For a first-afternoon proof of concept with zero project code:

```sh
sudo tcpdump -l -n -e 'arp or broadcast or multicast' 2>/dev/null \
 | awk '/ARP/ {print "ch 1 on 48 60"; fflush()}
        /5353/ {print "ch 2 on 67 40"; fflush()}
        /1900/ {print "ch 3 on 72 30"; fflush()}' \
 | sendmidi dev "IAC Driver Bus 1" --
```

(`-l` and `fflush()` keep it line-buffered. `sendmidi --` reads commands from stdin.) Pair it with long-release pads in the DAW so the missing note-offs don't matter. It isn't production-grade: no density cap, no note-off bookkeeping. It's an immediate way to *hear* your network and decide whether the idea is musically worth pursuing.

---

## 4. Suggested roadmap

1. **Day 1:** run the §3.4 pipeline to validate the musical idea on your actual network.
2. **Week 1: Method A core.**
   - Two-tier ingress (unprivileged sockets, optional Scapy for ARP).
   - Windowed feature aggregator.
   - Stable per-device identity (hashed MAC → scale degree / "leitmotif").
   - Quantizer on Ableton Link.
   - python-rtmidi output with a density limiter, active-note tracker and panic on exit.
3. **Week 2:** emit the versioned event schema over WebSocket and add the §3.1 browser mapping and visualizer UI.
4. **Later, if sample-accuracy or in-project recall matters:** the §3.3 shared-memory Rust plugin, reading the same events. Consider full JUCE (Method C) only if you need a commercial-grade GUI and AU/AAX distribution.

### Mapping sketch (ambient-first)

| Network feature | Musical parameter |
|---|---|
| Device identity (hashed MAC/OUI) | Scale degree / register: each device has its own recurring note |
| Protocol class (ARP / mDNS / SSDP / DHCP / NetBIOS) | MIDI channel → instrument / timbre |
| Per-protocol packet rate (EWMA, log-scaled) | CC1/CC11: filter cutoff, pad swell |
| Inter-arrival regularity (periodic beacon vs. burst) | Note length / arpeggiator on-off |
| New device seen (DHCP / first ARP) | Chord or scene change, bell accent |
| Device silent for N minutes | Its voice fades out (release tail) |
| Traffic entropy across devices | Harmonic tension (consonant ↔ suspended voicings) |
| mDNS service type (`_airplay`, `_googlecast`, `_printer`) | Instrument choice / sample selection |
