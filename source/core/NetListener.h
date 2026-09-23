// Unprivileged broadcast/multicast listener running on its own thread.
//
// Plain UDP sockets with address reuse, so they coexist with the OS's own
// mDNS/SSDP services: no packet capture, no admin rights. Multicast groups are
// joined on every local IPv4 interface given, because Windows machines often
// have virtual adapters (WSL, Hyper-V, VPN) and the default one may be wrong.
// Events go to the audio thread through a lock-free queue; nothing here ever
// blocks the audio thread.
#pragma once

#include "NetEvent.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace ltb
{
struct PortSpec
{
    uint8_t kind;
    uint16_t port;
    std::string group; // empty = broadcast only
};

std::vector<PortSpec> defaultPortSpecs();

// Short human-readable summary of a packet (e.g. "query _airplay._tcp"). Never throws.
std::string describePacket (uint8_t kind, const uint8_t* data, size_t size, uint16_t port);

class NetListener
{
public:
    using Sink = std::function<void (const NetEvent&)>;

    // `interfaces`: local IPv4 addresses to join multicast groups on ("0.0.0.0" = OS default).
    // `simulatedOnly`: open no sockets; packets arrive only through simulatePacket() (demos, tests).
    NetListener (std::vector<PortSpec> ports, std::vector<std::string> interfaces, Sink sink,
                 double newDeviceWarmupSeconds = 15.0, bool simulatedOnly = false);
    ~NetListener();

    void start();
    void stop(); // joins the thread; returns within ~0.3 s

    struct PortStatus
    {
        std::string name;
        bool ok;
        std::string detail;
    };
    std::vector<PortStatus> getStatus() const;

    // Feeds a packet through exactly the same path as a received one. Only for a simulated
    // listener: the event sink is single-producer, so it must not race the network thread.
    void simulatePacket (uint8_t kind, uint16_t port, uint32_t ipv4, const uint8_t* data, size_t size,
                         double secondsSinceStart);
    bool isSimulated() const noexcept { return simulated; }
    std::vector<std::string> getRecentLines() const; // newest last
    int getDeviceCount() const noexcept { return deviceCount.load(); }
    uint64_t getPacketCount() const noexcept { return packetCount.load(); }

private:
    void run();
    void handlePacket (const PortSpec& spec, uint32_t ipv4, const uint8_t* data, size_t size, double ageSeconds);

    std::vector<PortSpec> specs;
    std::vector<std::string> interfaces;
    Sink sink;
    double warmup;
    bool simulated;

    std::vector<intptr_t> sockets; // parallel to `openSpecs`
    std::vector<PortSpec> openSpecs;
    std::thread thread;
    std::atomic<bool> running { false };

    mutable std::mutex infoLock;
    std::vector<PortStatus> status;
    struct FeedLine
    {
        std::string text;
        int count;
    };
    std::deque<FeedLine> recent; // repeats within the last few lines are merged, with a count

    std::unordered_set<uint32_t> devices; // network thread only
    std::atomic<int> deviceCount { 0 };
    std::atomic<uint64_t> packetCount { 0 };
};

uint32_t hashAddress (uint32_t ipv4) noexcept;
} // namespace ltb
