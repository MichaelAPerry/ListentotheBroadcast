// Renders a demo of the real plugin driven by a scripted home network.
//
// The packets are simulated, but they go through the plugin's real code path: packet parsing,
// device identity, the live feed, the music engine and the synth. Output: demo.wav plus one PNG
// of the plugin window per video frame, which tools/make_demo.sh turns into MP3/MP4.
//
// Usage: ltb_render_demo <out-dir> [seconds] [preset name]
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Presets.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <random>

using namespace ltb;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 480; // 10 ms
constexpr int kFps = 15;

struct Packet
{
    double time;
    uint8_t kind;
    uint16_t port;
    uint32_t ip;
    std::vector<uint8_t> bytes;
};

uint32_t ip (int a, int b, int c, int d) { return (uint32_t) (a << 24 | b << 16 | c << 8 | d); }

std::vector<uint8_t> dns (const std::string& name, bool answer)
{
    std::vector<uint8_t> out = { 0, 0, (uint8_t) (answer ? 0x84 : 0), 0, 0, (uint8_t) (answer ? 0 : 1), 0,
                                 (uint8_t) (answer ? 1 : 0), 0, 0, 0, 0 };
    size_t start = 0;
    while (start <= name.size())
    {
        const auto dot = name.find ('.', start);
        const auto label = name.substr (start, dot == std::string::npos ? std::string::npos : dot - start);
        out.push_back ((uint8_t) label.size());
        out.insert (out.end(), label.begin(), label.end());
        if (dot == std::string::npos)
            break;
        start = dot + 1;
    }
    out.push_back (0);
    for (uint8_t b : { 0, 12, 0, 1 })
        out.push_back (b);
    if (answer) // a little record data, like a real answer
        out.insert (out.end(), 60, 0x20);
    return out;
}

std::vector<uint8_t> text (const std::string& s) { return { s.begin(), s.end() }; }

std::vector<uint8_t> dhcp (int type)
{
    std::vector<uint8_t> out (240, 0);
    out[0] = 1;
    out[236] = 0x63;
    out[237] = 0x82;
    out[238] = 0x53;
    out[239] = 0x63;
    for (int b : { 53, 1, type, 255 })
        out.push_back ((uint8_t) b);
    out.resize (300, 0);
    return out;
}

// A small household: who talks, on what, and roughly how often.
std::vector<Packet> script (double seconds, uint32_t seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> jitter (0.7, 1.3), offset (0.0, 1.0);
    std::vector<Packet> out;
    auto every = [&] (double from, double period, int burst, uint8_t kind, uint16_t port, uint32_t src,
                      std::function<std::vector<uint8_t>()> make) {
        for (double t = from + offset (rng) * period; t < seconds; t += period * jitter (rng))
            for (int i = 0; i < burst; ++i)
                out.push_back ({ t + i * 0.12, kind, port, src, make() });
    };

    const auto speaker = ip (192, 168, 1, 20), tv = ip (192, 168, 1, 31), laptop = ip (192, 168, 1, 44),
               printer = ip (192, 168, 1, 52), phone = ip (192, 168, 1, 60), nas = ip (192, 168, 1, 10),
               newPhone = ip (192, 168, 1, 77);

    every (0.2, 3.0, 1, kMdns, 5353, speaker, [] { return dns ("Living Room._airplay._tcp.local", true); });
    every (0.5, 6.0, 1, kMdns, 5353, tv, [] { return dns ("Bravia._googlecast._tcp.local", true); });
    every (1.0, 7.0, 3, kSsdp, 1900, tv, [] {
        return text ("NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nNT: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
                     "NTS: ssdp:alive\r\nLOCATION: http://192.168.1.31:8008/ssdp/device-desc.xml\r\n\r\n");
    });
    every (2.0, 9.0, 1, kMdns, 5353, laptop, [] { return dns ("_spotify-connect._tcp.local", false); });
    every (3.0, 12.0, 1, kLlmnr, 5355, laptop, [] { return dns ("DESKTOP-7F3K", false); });
    every (1.5, 5.0, 1, kLanSync, 57621, laptop, [] { return text ("{\"spotify\":{\"device\":\"laptop\"}}"); });
    every (4.0, 14.0, 1, kWsd, 3702, printer, [] {
        return text ("<soap:Envelope><soap:Body><wsd:Hello><wsa:EndpointReference>urn:uuid:printer</wsa:EndpointReference>"
                     "<wsd:Types>wprt:PrintDeviceType</wsd:Types></wsd:Hello></soap:Body></soap:Envelope>");
    });
    every (2.5, 11.0, 1, kMdns, 5353, printer, [] { return dns ("Office Printer._ipp._tcp.local", true); });
    every (0.8, 5.0, 1, kMdns, 5353, phone, [] { return dns ("_companion-link._tcp.local", false); });
    every (3.5, 8.0, 1, kNetbios, 137, nas, [] { return std::vector<uint8_t> (50, 0x41); });
    every (5.0, 10.0, 1, kLanSync, 17500, nas, [] {
        return text ("{\"host_int\": 1234567, \"version\": [2, 0], \"displayname\": \"\", \"port\": 17500}");
    });

    // A phone joins the Wi-Fi partway through.
    out.push_back ({ 22.0, kDhcp, 67, 0, dhcp (1) });
    out.push_back ({ 22.4, kDhcp, 67, 0, dhcp (3) });
    every (23.0, 3.0, 1, kMdns, 5353, newPhone, [] { return dns ("_airplay._tcp.local", false); });

    std::sort (out.begin(), out.end(), [] (const Packet& a, const Packet& b) { return a.time < b.time; });
    return out;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "demo-out");
    const double seconds = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 40.0;
    const juce::String presetName = argc > 3 ? juce::String (argv[3]) : juce::String ("Night Drift");
    outDir.deleteRecursively();
    outDir.createDirectory();

    ListenProcessor p;
    p.useSimulatedNetwork();
    p.setPlayConfigDetails (0, 2, kSampleRate, kBlock);
    for (int i = 0; i < presets::count(); ++i)
        if (presets::name (i).equalsIgnoreCase (presetName))
            p.setCurrentProgram (i);
    p.prepareToPlay (kSampleRate, kBlock);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
    auto* listenEditor = dynamic_cast<ListenEditor*> (editor.get());
    editor->setSize (1180, 760);

    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::FileOutputStream> (outDir.getChildFile ("demo.wav"));
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), kSampleRate, 2, 24, {}, 0));
    stream.release(); // owned by the writer now

    const auto packets = script (seconds, 20260923u);
    size_t next = 0;
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    const long totalBlocks = (long) (seconds * kSampleRate / kBlock);
    const double fadeFrom = seconds - 4.0;
    int frame = 0;
    double nextFrameTime = 0.0;

    for (long b = 0; b < totalBlocks; ++b)
    {
        const double t = b * kBlock / kSampleRate;
        while (next < packets.size() && packets[next].time <= t)
        {
            const auto& pk = packets[next++];
            p.simulatePacket (pk.kind, pk.port, pk.ip, pk.bytes.data(), pk.bytes.size(), pk.time);
        }
        p.processBlock (buffer, midi);
        if (t > fadeFrom) // gentle fade at the end of the clip
            buffer.applyGainRamp (0, kBlock, (float) std::max (0.0, (seconds - t) / 4.0),
                                  (float) std::max (0.0, (seconds - t - kBlock / kSampleRate) / 4.0));
        writer->writeFromAudioSampleBuffer (buffer, 0, kBlock);

        if (t + 1.0e-9 >= nextFrameTime)
        {
            listenEditor->refreshNow();
            const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream png (outDir.getChildFile (juce::String::formatted ("frame_%05d.png", frame++)));
            juce::PNGImageFormat().writeImageToStream (img, png);
            nextFrameTime += 1.0 / kFps;
        }
    }
    writer.reset();
    std::printf ("rendered %.0f s of '%s': %zu packets, %d frames -> %s\n", seconds, presetName.toRawUTF8(),
                 packets.size(), frame, outDir.getFullPathName().toRawUTF8());
    return 0;
}
