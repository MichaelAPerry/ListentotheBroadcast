#include "NetListener.h"

#include <chrono>
#include <cstring>
#include <regex>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <winsock2.h>
 #include <ws2tcpip.h>
using SocketHandle = SOCKET;
static constexpr SocketHandle kBadSocket = INVALID_SOCKET;
static void closeSocket (SocketHandle s) { closesocket (s); }
static std::string lastSocketError() { return "error " + std::to_string (WSAGetLastError()); }
#else
 #include <arpa/inet.h>
 #include <cerrno>
 #include <netinet/in.h>
 #include <sys/select.h>
 #include <sys/socket.h>
 #include <unistd.h>
using SocketHandle = int;
static constexpr SocketHandle kBadSocket = -1;
static void closeSocket (SocketHandle s) { ::close (s); }
static std::string lastSocketError() { return std::strerror (errno); }
#endif

namespace ltb
{
namespace
{
#if defined(_WIN32)
struct WinsockInit
{
    WinsockInit()
    {
        WSADATA data;
        WSAStartup (MAKEWORD (2, 2), &data);
    }
    ~WinsockInit() { WSACleanup(); }
};
#endif

SocketHandle toHandle (intptr_t s) { return (SocketHandle) s; }

// Returns an open socket or kBadSocket, with a reason in `why`.
SocketHandle openSocket (const PortSpec& spec, const std::vector<std::string>& interfaces, std::string& why)
{
    SocketHandle s = ::socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kBadSocket)
    {
        why = lastSocketError();
        return kBadSocket;
    }

    int one = 1;
    setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (const char*) &one, sizeof (one));
#ifdef SO_REUSEPORT
    setsockopt (s, SOL_SOCKET, SO_REUSEPORT, (const char*) &one, sizeof (one));
#endif
    setsockopt (s, SOL_SOCKET, SO_BROADCAST, (const char*) &one, sizeof (one));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons (spec.port);
    addr.sin_addr.s_addr = htonl (INADDR_ANY);
    if (::bind (s, (const sockaddr*) &addr, sizeof (addr)) != 0)
    {
        why = lastSocketError();
        closeSocket (s);
        return kBadSocket;
    }

    if (! spec.group.empty())
    {
        int joined = 0;
        for (const auto& iface : interfaces)
        {
            ip_mreq mreq {};
            if (inet_pton (AF_INET, spec.group.c_str(), &mreq.imr_multiaddr) != 1
                || inet_pton (AF_INET, iface.c_str(), &mreq.imr_interface) != 1)
                continue;
            // Fails harmlessly when two addresses share an adapter (already joined).
            if (setsockopt (s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*) &mreq, sizeof (mreq)) == 0)
                ++joined;
        }
        if (joined == 0)
        {
            why = "could not join " + spec.group;
            closeSocket (s);
            return kBadSocket;
        }
        why = "joined on " + std::to_string (joined) + " interface" + (joined == 1 ? "" : "s");
    }
    return s;
}

std::string readDnsName (const uint8_t* buf, size_t size, size_t off, int depth = 0)
{
    std::string out;
    while (off < size && depth < 8)
    {
        const uint8_t len = buf[off];
        if (len == 0)
            return out;
        if ((len & 0xC0) == 0xC0)
        {
            if (off + 1 >= size)
                break;
            const size_t ptr = (size_t) ((len & 0x3F) << 8 | buf[off + 1]);
            const auto rest = readDnsName (buf, size, ptr, depth + 1);
            return out.empty() ? rest : (rest.empty() ? out : out + "." + rest);
        }
        if (off + 1 + len > size)
            break;
        if (! out.empty())
            out += ".";
        out.append ((const char*) buf + off + 1, len);
        off += 1u + len;
    }
    return out;
}
} // namespace

uint32_t hashAddress (uint32_t ipv4) noexcept
{
    uint32_t h = 2166136261u; // FNV-1a
    for (int i = 0; i < 4; ++i)
    {
        h ^= (ipv4 >> (i * 8)) & 0xFFu;
        h *= 16777619u;
    }
    return h;
}

std::vector<PortSpec> defaultPortSpecs()
{
    std::vector<PortSpec> out;
    for (int k = 0; k < kNumKinds; ++k)
        for (auto port : kKinds[k].ports)
            if (port != 0)
                out.push_back ({ (uint8_t) k, port, kKinds[k].multicastGroup ? kKinds[k].multicastGroup : "" });
    return out;
}

std::string describePacket (uint8_t kind, const uint8_t* data, size_t size, uint16_t port)
{
    try
    {
        if ((kind == kMdns || kind == kLlmnr) && size > 12)
        {
            const bool answer = (data[2] & 0x80) != 0;
            auto name = readDnsName (data, size, 12);
            static const std::regex service (R"((_[\w-]+\._(?:tcp|udp)))");
            std::smatch m;
            if (std::regex_search (name, m, service))
                name = m[1];
            return (answer ? "answer " : "query ") + name.substr (0, 60);
        }
        if (kind == kSsdp || kind == kWsd)
        {
            std::string text ((const char*) data, std::min<size_t> (size, 1024));
            if (kind == kWsd)
            {
                for (const char* verb : { "ProbeMatches", "Probe", "Hello", "Bye", "Resolve" })
                    if (text.find (verb) != std::string::npos)
                        return verb;
                return "";
            }
            std::string first = text.substr (0, text.find (' '));
            static const std::regex target (R"((?:^|\r\n)(?:NT|ST|nt|st):\s*([^\r\n]+))");
            std::smatch m;
            if (std::regex_search (text, m, target))
                first += " " + m[1].str().substr (0, 60);
            return first;
        }
        if (kind == kDhcp && size > 240 && data[236] == 0x63 && data[237] == 0x82 && data[238] == 0x53 && data[239] == 0x63)
        {
            static const char* types[] = { "", "DISCOVER", "OFFER", "REQUEST", "DECLINE", "ACK", "NAK", "RELEASE", "INFORM" };
            for (size_t off = 240; off + 2 < size;)
            {
                const uint8_t opt = data[off];
                if (opt == 255)
                    break;
                if (opt == 0)
                {
                    ++off;
                    continue;
                }
                if (opt == 53 && data[off + 1] >= 1 && data[off + 2] <= 8)
                    return types[data[off + 2]];
                off += 2u + data[off + 1];
            }
        }
        if (kind == kLanSync)
            return port == 17500 ? "Dropbox" : port == 57621 ? "Spotify" : port == 27036 ? "Steam" : "";
    }
    catch (...)
    {
    }
    return "";
}

NetListener::NetListener (std::vector<PortSpec> p, std::vector<std::string> ifaces, Sink s, double warmupSeconds)
    : specs (std::move (p)), interfaces (std::move (ifaces)), sink (std::move (s)), warmup (warmupSeconds)
{
    if (interfaces.empty())
        interfaces.push_back ("0.0.0.0");
}

NetListener::~NetListener() { stop(); }

void NetListener::start()
{
    if (running.exchange (true))
        return;
#if defined(_WIN32)
    static WinsockInit winsock;
#endif
    std::vector<PortStatus> st;
    for (const auto& spec : specs)
    {
        std::string why;
        const auto s = openSocket (spec, interfaces, why);
        const std::string name = std::string (kKinds[spec.kind].id) + ":" + std::to_string (spec.port);
        st.push_back ({ name, s != kBadSocket, why });
        if (s != kBadSocket)
        {
            sockets.push_back ((intptr_t) s);
            openSpecs.push_back (spec);
        }
    }
    {
        std::lock_guard<std::mutex> g (infoLock);
        status = std::move (st);
    }
    thread = std::thread ([this] { run(); });
}

void NetListener::stop()
{
    if (! running.exchange (false))
        return;
    if (thread.joinable())
        thread.join();
    for (auto s : sockets)
        closeSocket (toHandle (s));
    sockets.clear();
    openSpecs.clear();
}

std::vector<NetListener::PortStatus> NetListener::getStatus() const
{
    std::lock_guard<std::mutex> g (infoLock);
    return status;
}

std::vector<std::string> NetListener::getRecentLines() const
{
    std::lock_guard<std::mutex> g (infoLock);
    return { recent.begin(), recent.end() };
}

void NetListener::run()
{
    using clock = std::chrono::steady_clock;
    const auto started = clock::now();
    std::vector<uint8_t> buf (9000);

    while (running.load())
    {
        if (sockets.empty())
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (200));
            continue;
        }
        fd_set readable;
        FD_ZERO (&readable);
        SocketHandle maxFd = 0;
        for (auto s : sockets)
        {
            FD_SET (toHandle (s), &readable);
            maxFd = std::max (maxFd, toHandle (s));
        }
        timeval tv { 0, 200000 };
        if (select ((int) maxFd + 1, &readable, nullptr, nullptr, &tv) <= 0)
            continue;

        for (size_t i = 0; i < sockets.size(); ++i)
        {
            const auto s = toHandle (sockets[i]);
            if (! FD_ISSET (s, &readable))
                continue;
            sockaddr_in from {};
            socklen_t fromLen = sizeof (from);
            const auto n = recvfrom (s, (char*) buf.data(), (int) buf.size(), 0, (sockaddr*) &from, &fromLen);
            if (n <= 0)
                continue;

            const auto& spec = openSpecs[i];
            const uint32_t ip = ntohl (from.sin_addr.s_addr);
            NetEvent ev;
            ev.kind = spec.kind;
            ev.size = (uint16_t) std::min<long> ((long) n, 65535);
            ev.device = hashAddress (ip);
            const bool isNew = devices.insert (ev.device).second;
            const double age = std::chrono::duration<double> (clock::now() - started).count();
            ev.newDevice = isNew && age > warmup;
            deviceCount.store ((int) devices.size());
            packetCount.fetch_add (1);
            sink (ev);

            char ipText[INET_ADDRSTRLEN] = {};
            inet_ntop (AF_INET, &from.sin_addr, ipText, sizeof (ipText));
            std::string line = std::string (kKinds[spec.kind].id) + "  " + ipText + "  "
                               + std::to_string (n) + "B  " + describePacket (spec.kind, buf.data(), (size_t) n, spec.port);
            if (ev.newDevice)
                line = "+ new device " + std::string (ipText);
            std::lock_guard<std::mutex> g (infoLock);
            recent.push_back (std::move (line));
            while (recent.size() > 200)
                recent.pop_front();
        }
    }
}
} // namespace ltb
