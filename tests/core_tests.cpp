// Unit tests for the JUCE-free core. Plain asserts, returns non-zero on failure.
#include "../source/core/AmbientSynth.h"
#include "../source/core/MusicEngine.h"
#include "../source/core/NetListener.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <winsock2.h>
 #include <ws2tcpip.h>
#else
 #include <arpa/inet.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
 #include <unistd.h>
#endif

using namespace ltb;

static int failures = 0;
#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (! (cond))                                                          \
        {                                                                      \
            std::printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

struct Recorder : NoteSink
{
    struct On
    {
        NoteOn note;
        int offset;
        int block;
    };
    std::vector<On> ons;
    std::vector<std::pair<uint32_t, int>> offs;
    int block = 0;
    void noteOn (const NoteOn& n, int offset) override { ons.push_back ({ n, offset, block }); }
    void noteOff (uint32_t id, uint8_t, float, int offset) override { offs.push_back ({ id, offset }); }
};

// Runs the engine at 120 bpm / 48 kHz in 375-sample blocks = exactly 1/64 of a quarter note,
// so a 16th is 16 blocks and a bar is 256 blocks, with no floating-point drift.
struct Rig
{
    MusicEngine engine;
    EngineSettings settings = EngineSettings::defaults();
    Recorder rec;
    double base = 0.0;
    long n = 0;
    static constexpr double kSpq = 24000.0;
    static constexpr int kBlock = 375;

    void jumpTo (double ppq)
    {
        base = ppq;
        n = 0;
    }
    double noteTime (const Recorder::On& o) const { return o.block / 64.0 + o.offset / kSpq; }
    void run (int blocks)
    {
        for (int i = 0; i < blocks; ++i)
        {
            engine.processBlock (settings, base + n * (kBlock / kSpq), kSpq, kBlock, rec);
            ++n;
            ++rec.block;
        }
    }
    void only (int kind)
    {
        for (int k = 0; k < kNumKinds; ++k)
            settings.rows[(size_t) k].on = (k == kind);
    }
    void event (int kind, uint32_t device = 1, uint16_t size = 100, bool isNew = false)
    {
        NetEvent e;
        e.kind = (uint8_t) kind;
        e.device = device;
        e.size = size;
        e.newDevice = isNew;
        engine.addEvent (e);
    }
};

static void testScales()
{
    std::printf ("scales\n");
    CHECK (kNumScales >= 45);
    std::set<std::string> families;
    for (const auto& s : kScales)
    {
        families.insert (s.family);
        CHECK (s.steps[0] == 0.0f);
        for (int i = 1; i < s.numSteps; ++i)
            CHECK (s.steps[(size_t) i] > s.steps[(size_t) i - 1] && s.steps[(size_t) i] < 12.0f);
    }
    CHECK (families.size() == 7);
    const auto& major = kScales[0];
    CHECK (degreeToSemitones (major, 7) == 12.0f);
    CHECK (degreeToSemitones (major, 9) == 16.0f);
    CHECK (degreeToSemitones (major, -1) == -1.0f);
    int microtonal = 0;
    for (const auto& s : kScales)
        microtonal += s.isMicrotonal() ? 1 : 0;
    CHECK (microtonal == 5); // four maqamat + slendro
}

static void testQueue()
{
    std::printf ("queue\n");
    SpscQueue<int, 4> q;
    for (int i = 0; i < 4; ++i)
        CHECK (q.push (i));
    CHECK (! q.push (99)); // full: dropped, never blocks
    int v = -1;
    CHECK (q.pop (v) && v == 0);
    CHECK (q.push (4));
    for (int expected = 1; expected <= 4; ++expected)
        CHECK (q.pop (v) && v == expected);
    CHECK (! q.pop (v));
}

static void testGridAndScale()
{
    std::printf ("engine: notes land on the grid, in the scale\n");
    Rig r;
    r.only (kMdns);
    r.settings.rows[kMdns].division = 2; // quarters
    r.settings.rows[kMdns].chance = 1.0f;
    r.settings.scale = 37; // Hirajoshi
    r.settings.root = 0;
    r.run (13); // just past beat 1
    r.event (kMdns);
    r.run (51); // up to (not including) beat 2 at ppq 1.0
    CHECK (r.rec.ons.empty());
    r.run (1);
    CHECK (r.rec.ons.size() == 1);
    if (! r.rec.ons.empty())
    {
        CHECK (r.rec.ons[0].block == 64 && r.rec.ons[0].offset == 0);
        const int pc = ((int) std::lround (r.rec.ons[0].note.pitch) % 12 + 12) % 12;
        std::set<int> allowed;
        for (int i = 0; i < kScales[37].numSteps; ++i)
            allowed.insert ((int) kScales[37].steps[(size_t) i]);
        CHECK (std::string (kScales[37].name) == "Hirajoshi");
        CHECK (allowed.count (pc) == 1);
        CHECK (r.rec.ons[0].note.channel == 1 && r.rec.ons[0].note.mono);
    }
}

static void testDevicesHaveStableNotes()
{
    std::printf ("engine: each device keeps its note\n");
    Rig r;
    r.only (kMdns);
    r.settings.rows[kMdns].division = 0;
    r.settings.rows[kMdns].chance = 1.0f;
    r.settings.rows[kMdns].density = 16;
    r.settings.progression = 0; // hold
    std::map<uint32_t, std::set<float>> pitches;
    for (uint32_t d : { 11u, 11u, 12u, 13u, 14u, 11u })
    {
        r.event (kMdns, d);
        const auto before = r.rec.ons.size();
        r.run (16); // one 16th
        CHECK (r.rec.ons.size() == before + 1);
        if (r.rec.ons.size() > before)
            pitches[d].insert (r.rec.ons.back().note.pitch);
    }
    CHECK (pitches[11].size() == 1);
    std::set<float> all;
    for (auto& [d, p] : pitches)
        all.insert (p.begin(), p.end());
    CHECK (all.size() > 1);
}

static void testEveryNoteEnds()
{
    std::printf ("engine: every note-on gets a note-off\n");
    Rig r;
    for (int i = 0; i < 400; ++i)
    {
        r.event (i % kNumKinds, (uint32_t) (i % 9), (uint16_t) (60 + i));
        r.run (3);
    }
    r.run (256 * 24); // 24 bars: activity decays and the last pad (4 bars long) ends
    std::set<uint32_t> on, off;
    for (auto& o : r.rec.ons)
        on.insert (o.note.id);
    for (auto& o : r.rec.offs)
        off.insert (o.first);
    CHECK (! on.empty());
    CHECK (on == off);
    CHECK (r.engine.getActiveNoteCount() == 0);
}

static void testDensityCap()
{
    std::printf ("engine: density cap\n");
    Rig r;
    r.only (kLlmnr);
    r.settings.rows[kLlmnr].division = 0;
    r.settings.rows[kLlmnr].density = 2;
    r.settings.rows[kLlmnr].chance = 1.0f;
    for (int i = 0; i < 16; ++i)
    {
        r.event (kLlmnr);
        r.run (16);
    }
    int firstBar = 0;
    for (auto& o : r.rec.ons)
        firstBar += r.noteTime (o) < 4.0 ? 1 : 0;
    CHECK (firstBar == 2);
}

static void testProgressionFollowsBars()
{
    std::printf ("engine: progression follows the bar number\n");
    Rig r;
    r.settings.progression = 2; // pop I-V-vi-IV
    r.settings.changeBars = 1;
    std::vector<int> roots;
    for (int bar = 0; bar < 5; ++bar)
    {
        r.run (256); // one bar
        roots.push_back (r.engine.chordRootDegree (r.settings));
    }
    CHECK ((roots == std::vector<int> { 0, 4, 5, 3, 0 }));

    // Jumping the transport (host loop) recomputes the chord from the new position.
    r.jumpTo (8.0); // bar 3 → vi
    r.run (1);
    CHECK (r.engine.chordRootDegree (r.settings) == 5);
}

static void testPadRevoicesOnChordChange()
{
    std::printf ("engine: pads sustain and revoice on chord changes\n");
    Rig r;
    r.only (kSsdp);
    r.settings.changeBars = 2;
    r.settings.progression = 2;
    r.settings.rows[kSsdp].length = 256;
    for (int i = 0; i < 4 * 16; ++i) // four bars
    {
        r.event (kSsdp);
        r.run (16);
    }
    // voiced in bar 1, then again at bar 3 (chord change): 2 x 3 notes
    CHECK (r.rec.ons.size() == 6);
    CHECK (r.rec.offs.size() == 3); // the first voicing was released
}

static void testTestTrigger()
{
    std::printf ("engine: test trigger plays immediately\n");
    Rig r;
    r.only (-1);
    r.engine.triggerTest (kNetbios);
    r.run (1);
    CHECK (r.rec.ons.size() == 1 && r.rec.ons[0].note.channel == 6 && r.rec.ons[0].offset == 0);
}

static void testSwingAndNegativePpq()
{
    std::printf ("engine: swing delays off-beats; negative positions are safe\n");
    Rig r;
    r.only (kMdns);
    r.settings.rows[kMdns].division = 0;
    r.settings.rows[kMdns].chance = 1.0f;
    r.settings.swing = 1.0f;
    r.jumpTo (0.25 - 1.0 / 64); // just before step 1 (an off-beat 16th)
    r.event (kMdns);
    r.run (4); // past the straight position (0.25)
    CHECK (r.rec.ons.empty()); // swung: lands a 32nd later, at 0.375
    r.run (6);
    CHECK (r.rec.ons.size() == 1);
    if (! r.rec.ons.empty())
        CHECK (std::abs (0.25 - 1.0 / 64 + r.noteTime (r.rec.ons[0]) - 0.375) < 1.0e-4);

    Rig n;
    n.jumpTo (-3.0); // host pre-roll
    n.event (kDhcp);
    n.run (400);
    CHECK (! n.rec.ons.empty());
}

static void testSynth()
{
    std::printf ("synth: every sound plays and then goes quiet\n");
    for (int sound = 0; sound < kSoundMidiOnly; ++sound)
    {
        AmbientSynth synth;
        synth.prepare (48000.0);
        synth.noteOn (1, sound, sound == kSoundHit ? 42.0f : 63.5f, 0.8f);
        std::vector<float> l (4800, 0.0f), rr (4800, 0.0f);
        synth.render (l.data(), rr.data(), 4800);
        float peak = 0.0f;
        bool finite = true;
        for (size_t i = 0; i < l.size(); ++i)
        {
            peak = std::max (peak, std::abs (l[i]) + std::abs (rr[i]));
            finite = finite && std::isfinite (l[i]) && std::isfinite (rr[i]);
        }
        CHECK (finite);
        CHECK (peak > 0.005f && peak < 1.5f);
        synth.noteOff (1);
        for (int i = 0; i < 100 && synth.getActiveVoiceCount() > 0; ++i)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (rr.begin(), rr.end(), 0.0f);
            synth.render (l.data(), rr.data(), 4800);
        }
        CHECK (synth.getActiveVoiceCount() == 0);
    }
    AmbientSynth synth;
    synth.prepare (44100.0);
    for (uint32_t i = 0; i < 200; ++i) // more notes than voices: steals, never crashes
        synth.noteOn (i, kSoundPad, 60.0f + (float) (i % 12), 1.0f);
    CHECK (synth.getActiveVoiceCount() == AmbientSynth::kMaxVoices);
    synth.allNotesOff();
    std::vector<float> l (44100, 0.0f), rr (44100, 0.0f);
    synth.render (l.data(), rr.data(), 44100);
    CHECK (synth.getActiveVoiceCount() == 0);
}

static void testDescribe()
{
    std::printf ("describePacket\n");
    const uint8_t mdns[] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 8, '_', 'a', 'i', 'r', 'p', 'l', 'a', 'y',
                             4, '_', 't', 'c', 'p', 5, 'l', 'o', 'c', 'a', 'l', 0, 0, 12, 0, 1 };
    CHECK (describePacket (kMdns, mdns, sizeof (mdns), 5353) == "query _airplay._tcp");
    const char* ssdp = "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nNT: upnp:rootdevice\r\n\r\n";
    CHECK (describePacket (kSsdp, (const uint8_t*) ssdp, std::strlen (ssdp), 1900) == "NOTIFY upnp:rootdevice");
    const uint8_t loop[] = { 0, 0, 0x84, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0xC0, 0x0C };
    describePacket (kMdns, loop, sizeof (loop), 5353); // self-pointer: must terminate
    std::vector<uint8_t> junk (7, 0xFF);
    for (int k = 0; k < kNumKinds; ++k)
        describePacket ((uint8_t) k, junk.data(), junk.size(), 0);
    CHECK (describePacket (kLanSync, junk.data(), 0, 57621) == "Spotify");
}

static void testListener()
{
    std::printf ("listener: receives broadcast-port and multicast UDP without privileges\n");
    std::mutex m;
    std::vector<NetEvent> got;
    std::vector<PortSpec> specs = { { kLanSync, 45354, "" }, { kSsdp, 45355, "239.255.77.77" } };
    NetListener listener (specs, {}, [&] (const NetEvent& e) {
        std::lock_guard<std::mutex> g (m);
        got.push_back (e);
    }, 0.0);
    listener.start();
    for (auto& st : listener.getStatus())
    {
        std::printf ("  %s %s %s\n", st.name.c_str(), st.ok ? "ok" : "FAILED", st.detail.c_str());
        CHECK (st.ok);
    }

    auto s = ::socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const char msg[] = "NOTIFY * HTTP/1.1\r\nNT: test\r\n\r\n";
    sockaddr_in to {};
    to.sin_family = AF_INET;
    to.sin_port = htons (45354);
    inet_pton (AF_INET, "127.0.0.1", &to.sin_addr);
    sendto (s, msg, (int) sizeof (msg), 0, (sockaddr*) &to, sizeof (to));
    to.sin_port = htons (45355);
    inet_pton (AF_INET, "239.255.77.77", &to.sin_addr);
    unsigned char loopOn = 1;
    setsockopt (s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*) &loopOn, sizeof (loopOn));
    const bool multicastSent = sendto (s, msg, (int) sizeof (msg), 0, (sockaddr*) &to, sizeof (to)) > 0;

    for (int i = 0; i < 40; ++i)
    {
        {
            std::lock_guard<std::mutex> g (m);
            if (got.size() >= (multicastSent ? 2u : 1u))
                break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
    {
        std::lock_guard<std::mutex> g (m);
        bool sawBroadcastPort = false, sawMulticast = false;
        for (auto& e : got)
        {
            sawBroadcastPort = sawBroadcastPort || e.kind == kLanSync;
            sawMulticast = sawMulticast || e.kind == kSsdp;
        }
        CHECK (sawBroadcastPort);
        if (multicastSent)
            CHECK (sawMulticast);
        else
            std::printf ("  (no multicast route in this environment; skipped multicast check)\n");
        CHECK (listener.getDeviceCount() >= 1);
        CHECK (! listener.getRecentLines().empty());
    }
#if defined(_WIN32)
    closesocket (s);
#else
    close (s);
#endif
    const auto t0 = std::chrono::steady_clock::now();
    listener.stop();
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  stop took %.0f ms\n", ms);
    CHECK (ms < 1000.0);
}

int main()
{
    testScales();
    testQueue();
    testGridAndScale();
    testDevicesHaveStableNotes();
    testEveryNoteEnds();
    testDensityCap();
    testProgressionFollowsBars();
    testPadRevoicesOnChordChange();
    testTestTrigger();
    testSwingAndNegativePpq();
    testSynth();
    testDescribe();
    testListener();
    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nall core tests passed\n", failures);
    return failures ? 1 : 0;
}
