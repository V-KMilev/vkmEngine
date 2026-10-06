#define VKM_LOG_CATEGORY "NET"

#include "platform/net/net_address.h"

#include <cstdio>

#include "logger.h"

#include "platform/net/winsock_init.h"

#include "platform/windows_api.h"

#if defined(_WIN32)
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
#endif

namespace Vkm::Engine {

namespace {

/// Ask the system resolver, for the names inet_pton will not take.
bool resolveHost(const std::string& host, uint32_t& out) {
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* found = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || !found) return false;

    const auto* address = reinterpret_cast<const sockaddr_in*>(found->ai_addr);
    out = ntohl(address->sin_addr.s_addr);
    ::freeaddrinfo(found);
    return true;
}

} // namespace

NetAddress NetAddress::parse(const std::string& text, uint16_t fallbackPort) {
    if (text.empty()) return {};

    // inet_pton and the resolver are winsock entry points; no socket may exist yet.
    if (!ensureWinsock()) return {};

    // On the last colon, so a bracketed form cannot split on its own separators.
    std::string host = text;
    uint16_t    port = fallbackPort;

    const size_t colon = text.rfind(':');
    if (colon != std::string::npos) {
        host = text.substr(0, colon);
        // "host:" and "host:abc" are refused, not given the fallback.
        if (!parsePort(text.substr(colon + 1), port)) return {};
    }

    if (host.empty() || port == 0) return {};

    NetAddress address;
    address.port = port;

    in_addr binary{};
    if (::inet_pton(AF_INET, host.c_str(), &binary) == 1) {
        address.ipv4 = ntohl(binary.s_addr);
        return address;
    }

    if (!resolveHost(host, address.ipv4)) {
        LOG_WARNING("'%s' does not name a host this machine can reach", host.c_str());
        return {};
    }
    return address;
}

bool NetAddress::parsePort(const std::string& text, uint16_t& port) {
    if (text.empty()) return false;
    uint32_t parsed = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        parsed = parsed * 10u + static_cast<uint32_t>(c - '0');
        if (parsed > 65535u) return false;
    }
    if (parsed == 0) return false;
    port = static_cast<uint16_t>(parsed);
    return true;
}

std::string NetAddress::toString() const {
    char text[32];
    std::snprintf(
        text,
        sizeof(text),
        "%u.%u.%u.%u:%u",
        (ipv4 >> 24) & 0xFFu,
        (ipv4 >> 16) & 0xFFu,
        (ipv4 >> 8) & 0xFFu,
        ipv4 & 0xFFu,
        port
    );
    return text;
}

} // namespace Vkm::Engine
