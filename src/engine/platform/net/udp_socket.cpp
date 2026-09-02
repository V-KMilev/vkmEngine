#define VKM_LOG_CATEGORY "NET"

#include "platform/net/udp_socket.h"

#include <cstring>

#include "logger.h"

#include "platform/net/winsock_init.h"

#if defined(_WIN32)
    // NOGDI is not optional here; see platform/net/winsock_init.h for why, and
    // why the other two are guarded.
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #define NOGDI
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mstcpip.h>   // SIO_UDP_CONNRESET, on the toolchains that have it

    // mingw-w64 ships mstcpip.h without this one. A documented, stable control
    // code, so defining it where the toolchain does not beats giving up what it
    // buys; guarded, so a toolchain that declares it keeps its own.
    #ifndef SIO_UDP_CONNRESET
        #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
    #endif
    using NativeSocket = SOCKET;

    #define VKM_CLOSE_SOCKET closesocket
    #define VKM_WOULD_BLOCK  (WSAGetLastError() == WSAEWOULDBLOCK)
    #define VKM_MSG_TOO_LONG (WSAGetLastError() == WSAEMSGSIZE)
#else
    #include <arpa/inet.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    using NativeSocket = int;
    #define VKM_CLOSE_SOCKET ::close
    #define VKM_WOULD_BLOCK  (errno == EAGAIN || errno == EWOULDBLOCK)
    #define VKM_MSG_TOO_LONG (errno == EMSGSIZE)
#endif

namespace Vkm::Engine {

namespace {

sockaddr_in toSockaddr(const NetAddress& address) {
    sockaddr_in out{};
    out.sin_family      = AF_INET;
    out.sin_addr.s_addr = htonl(address.ipv4);
    out.sin_port        = htons(address.port);
    return out;
}

} // namespace

UdpSocket::~UdpSocket() {
    close();
}

bool UdpSocket::open(uint16_t port) {
    close();
    if (!ensureWinsock()) return false;

    const NativeSocket handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == static_cast<NativeSocket>(INVALID)) {
        LOG_ERROR("Could not create a UDP socket");
        return false;
    }

    sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port        = htons(port);

    if (::bind(handle, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        LOG_ERROR("Port %u is not available", port);
        VKM_CLOSE_SOCKET(handle);
        return false;
    }

    // Non-blocking, which is the whole reason this class exists: a frame that
    // waits on a network is a frame that has stopped drawing.
#if defined(_WIN32)
    // Windows alone answers a peer's ICMP "port unreachable" by failing this
    // socket's *next* read, about an unrelated datagram - so a dead client ends
    // a live one's read loop. See networking.md, "The connection".
    BOOL  reportUnreachable = FALSE;
    DWORD ignored           = 0;
    if (::WSAIoctl(handle, SIO_UDP_CONNRESET, &reportUnreachable, sizeof(reportUnreachable),
                   nullptr, 0, &ignored, nullptr, nullptr) != 0) {
        LOG_WARNING("Could not turn off connection-reset reporting; a peer that goes "
                    "away will cut this socket's reads short");
    }

    u_long nonBlocking = 1;
    const bool set = ioctlsocket(handle, FIONBIO, &nonBlocking) == 0;
#else
    const int flags = ::fcntl(handle, F_GETFL, 0);
    const bool set  = flags != -1 && ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    if (!set) {
        LOG_ERROR("A UDP socket would not go non-blocking, and a blocking one "
                  "would stall the frame");
        VKM_CLOSE_SOCKET(handle);
        return false;
    }

    // Asked back rather than assumed: a caller that passed zero wants the port
    // the system chose, and that is the only way to learn it.
    sockaddr_in bound{};
    socklen_t   boundSize = sizeof(bound);
    m_local.port = ::getsockname(handle, reinterpret_cast<sockaddr*>(&bound), &boundSize) == 0
                 ? ntohs(bound.sin_port)
                 : port;

    // Loopback, not because that is where the socket is: bound to every
    // interface getsockname answers 0.0.0.0, which nothing can connect to.
    // What this member is for is the port.
    m_local.ipv4 = 0x7F000001u;

    m_handle = static_cast<intptr_t>(handle);
    return true;
}

void UdpSocket::close() {
    if (m_handle == INVALID) return;

    VKM_CLOSE_SOCKET(static_cast<NativeSocket>(m_handle));
    m_handle      = INVALID;
    m_local       = {};
    m_readFailed  = false;
    m_writeFailed = false;
    m_oversized   = false;
}

bool UdpSocket::send(const NetAddress& to, const uint8_t* bytes, size_t size) {
    if (m_handle == INVALID || !to || !bytes || size == 0) return false;

    if (size > MAX_DATAGRAM) {
        LOG_ERROR("Refusing a %zu byte datagram: past %zu it is fragmented, and "
                  "one lost fragment loses all of it", size, MAX_DATAGRAM);
        return false;
    }

    const sockaddr_in target = toSockaddr(to);
    const auto written = ::sendto(static_cast<NativeSocket>(m_handle),
                                  reinterpret_cast<const char*>(bytes),
                                  static_cast<int>(size), 0,
                                  reinterpret_cast<const sockaddr*>(&target), sizeof(target));
    if (written < 0 || static_cast<size_t>(written) != size) {
        // Once per socket, like the read latch: whatever stops a send usually
        // stops the next one too, at the frame rate.
        if (!m_writeFailed) {
            m_writeFailed = true;
            LOG_WARNING("A UDP send to %s failed; further failures on this socket "
                        "are not repeated", to.toString().c_str());
        }
        return false;
    }
    m_writeFailed = false;
    return true;
}

void UdpSocket::noteOversized(size_t bytes) {
    // Once. Anyone can send a datagram, so a line per packet is a log somebody
    // else decides the size of.
    if (m_oversized) return;
    m_oversized = true;

    // Zero means the platform refused the read without saying how big it was,
    // which is all Windows reports.
    if (bytes > 0) {
        LOG_WARNING("Dropping a %zu byte datagram: nothing here sends past %zu; "
                    "further oversized ones on this socket are not repeated",
                    bytes, MAX_DATAGRAM);
    } else {
        LOG_WARNING("Dropping a datagram larger than the %zu this end reads; "
                    "further oversized ones on this socket are not repeated",
                    MAX_DATAGRAM);
    }
}

bool UdpSocket::receive(std::vector<uint8_t>& out, NetAddress& from) {
    out.clear();
    if (m_handle == INVALID) return false;

    // Loops rather than returning: a caller draining the socket reads false as
    // "nothing more this frame", so one stray oversized packet would stop a
    // server reading the rest of its traffic that frame.
    for (;;) {
        // One byte of headroom, so a datagram too big to be ours is seen to be
        // too big rather than silently truncated to exactly the legal size.
        uint8_t     buffer[MAX_DATAGRAM + 1];
        sockaddr_in sender{};
        socklen_t   senderSize = sizeof(sender);

        const auto read = ::recvfrom(static_cast<NativeSocket>(m_handle),
                                     reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                     reinterpret_cast<sockaddr*>(&sender), &senderSize);
        if (read < 0) {
            // Windows reports a datagram too big for the buffer as an error
            // where a POSIX read hands back the truncated head. The same packet
            // deserves the same answer, so it takes the oversized path below
            // rather than reading as a broken socket and ending the drain.
            if (VKM_MSG_TOO_LONG) {
                noteOversized(0);
                continue;
            }
            // Nothing waiting is the common case, not a failure. Anything else
            // is worth saying once - a broken socket looks like a quiet one from
            // here - but only once, since the cause repeats at the frame rate.
            if (!VKM_WOULD_BLOCK && !m_readFailed) {
                m_readFailed = true;
                LOG_WARNING("A UDP read failed for a reason other than being empty; "
                            "further failures on this socket are not repeated");
            }
            return false;
        }
        m_readFailed = false;

        // A datagram of no bytes is a packet that arrived, not an empty socket.
        // Read as the latter it would end this frame's drain, which is the one
        // thing this loop exists to prevent - and anyone can send one.
        if (read == 0) continue;

        if (static_cast<size_t>(read) > MAX_DATAGRAM) {
            noteOversized(static_cast<size_t>(read));
            continue;
        }

        out.assign(buffer, buffer + read);
        from.ipv4 = ntohl(sender.sin_addr.s_addr);
        from.port = ntohs(sender.sin_port);
        return true;
    }
}

} // namespace Vkm::Engine
