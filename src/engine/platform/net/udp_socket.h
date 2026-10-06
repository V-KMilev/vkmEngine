#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "platform/net/net_address.h"

namespace Vkm::Engine {

/**
 * @brief A non-blocking UDP socket, the same on Windows and Linux.
 *
 * The whole Winsock/BSD difference sits behind this type. Non-blocking, so a
 * frame never stalls on the network.
 */
class UdpSocket {
    public:
        /**
         * @brief The largest datagram this will send.
         *
         * Inside the smallest path MTU worth designing for, with room for the
         * IP and UDP headers. Past it IP fragments, and one lost fragment loses
         * the whole datagram.
         */
        static constexpr size_t MAX_DATAGRAM = 1200;

    public:
        UdpSocket() = default;
        ~UdpSocket();

        UdpSocket(const UdpSocket& other) = delete;
        UdpSocket& operator=(const UdpSocket& other) = delete;

        UdpSocket(UdpSocket && other) = delete;
        UdpSocket& operator=(UdpSocket && other) = delete;

    public:
        /**
         * @brief Take a port and start listening on every interface.
         *
         * @param port Port to bind, or 0 to let the system choose a free one.
         * @return Whether the socket is open.
         */
        [[nodiscard]] bool open(uint16_t port);

        /// Close it. Safe to call on one that was never opened.
        void close();

        /**
         * @brief Send @p size bytes to @p to.
         *
         * Refused past MAX_DATAGRAM.
         *
         * @param to    Where it goes.
         * @param bytes The payload.
         * @param size  Payload length in bytes.
         * @return False when there is no socket, nowhere to send, it will not
         *         fit, or the send failed.
         */
        bool send(const NetAddress& to, const uint8_t* bytes, size_t size);

        /**
         * @brief Take the next datagram, if one is waiting.
         *
         * One datagram per call. An empty or oversized one answers true with
         * @p out empty, so a caller's per-frame read bound counts it.
         *
         * @param out  Filled with the payload; cleared first.
         * @param from Filled with where it came from.
         * @return False when nothing is waiting, which is not an error.
         */
        [[nodiscard]] bool receive(std::vector<uint8_t>& out, NetAddress& from);

        /**
         * @brief Loopback and the port this is bound to, including one the
         *        system picked for zero.
         *
         * 127.0.0.1 rather than the bound 0.0.0.0, which nothing can send to.
         *
         * @return The local address; null when the socket is closed.
         */
        NetAddress localAddress() const { return m_local; }

        bool isOpen() const { return m_handle != INVALID; }

    private:
        /**
         * @brief Sentinel for "no socket", held wide enough for POSIX and Windows.
         */
        static constexpr intptr_t INVALID = -1;

    private:
        /**
         * @brief Say once that a datagram was too big to be ours, and drop it.
         *
         * Neither platform reports the true size, so the log cannot either.
         */
        void noteOversized();

    private:
        intptr_t   m_handle = INVALID;

        bool       m_readFailed  = false;  ///< A read failed; say so once, not per frame.
        bool       m_oversized   = false;  ///< One arrived too big; likewise, once.
        bool       m_writeFailed = false;  ///< The same, for a send.
        NetAddress m_local;
};

} // namespace Vkm::Engine
