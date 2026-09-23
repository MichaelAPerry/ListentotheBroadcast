// Traffic types and the event record passed from the network thread to the audio thread.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ltb
{
enum Kind : uint8_t
{
    kMdns,
    kSsdp,
    kLlmnr,
    kWsd,
    kDhcp,
    kNetbios,
    kLanSync,
    kNumKinds
};

struct KindInfo
{
    const char* id; // parameter-id prefix, never change (saved in host projects)
    const char* label;
    const char* multicastGroup; // nullptr = plain broadcast
    std::array<uint16_t, 3> ports; // 0 = unused
};

inline constexpr KindInfo kKinds[kNumKinds] = {
    { "mdns", "mDNS / Bonjour", "224.0.0.251", { 5353, 0, 0 } },
    { "ssdp", "SSDP / UPnP", "239.255.255.250", { 1900, 0, 0 } },
    { "llmnr", "LLMNR (Windows names)", "224.0.0.252", { 5355, 0, 0 } },
    { "wsd", "WS-Discovery", "239.255.255.250", { 3702, 0, 0 } },
    { "dhcp", "DHCP (device joins)", nullptr, { 67, 68, 0 } },
    { "netbios", "NetBIOS", nullptr, { 137, 138, 0 } },
    { "lansync", "LAN sync (Dropbox/Spotify/Steam)", nullptr, { 17500, 57621, 27036 } },
};

struct NetEvent
{
    uint8_t kind = 0;
    bool newDevice = false; // first packet from this source after the warm-up period
    uint16_t size = 0;
    uint32_t device = 0; // stable hash of the source address
};

// Single-producer single-consumer lock-free ring. The network thread pushes,
// the audio thread pops. Never blocks and never allocates after construction;
// when full, new events are dropped.
template <typename T, size_t Capacity>
class SpscQueue
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push (const T& item) noexcept
    {
        const auto w = write.load (std::memory_order_relaxed);
        if (w - read.load (std::memory_order_acquire) >= Capacity)
            return false;
        items[w & (Capacity - 1)] = item;
        write.store (w + 1, std::memory_order_release);
        return true;
    }

    bool pop (T& item) noexcept
    {
        const auto r = read.load (std::memory_order_relaxed);
        if (r == write.load (std::memory_order_acquire))
            return false;
        item = items[r & (Capacity - 1)];
        read.store (r + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> items {};
    std::atomic<size_t> write { 0 }, read { 0 };
};
} // namespace ltb
