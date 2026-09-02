#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "platform/net/net_address.h"

namespace Vkm::Engine {

/**
 * @brief A non-blocking UDP socket, the same on Windows and Linux.
 *
 * The whole of the platform difference behind one type: Winsock and BSD
 * sockets differ in how they are started, how they are closed, how they are put
 * in non-blocking mode and how they say "nothing has arrived", and nothing
 * above this file needs to know any of it.
 *
 * Non-blocking because the alternative is a frame that stalls on the network.
 * receive() answering false means "nothing there", not "something went wrong",
 * which is the one difference in the two APIs that would otherwise reach a
 * caller.
 *
 * Resource-owning, so neither copyable nor movable: a handle that could be
 * duplicated is a handle that can be closed twice.
 */
class UdpSocket {
    public:
        /**
         * @brief The largest datagram this will send.
         *
         * Comfortably inside the smallest path MTU worth designing for, with
         * room for the IP and UDP headers that ride outside it. A datagram past
         * this is fragmented by IP, and a fragment lost loses the whole
         * datagram - so a packet that must not be fragmented is a packet that
         * must fit here.
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
         * @param port Port to bind, or 0 to let the system choose a free one -
         *             which is what a client wants, and what a test wants so
         *             two can run at once.
         * @return Whether the socket is open.
         */
        bool open(uint16_t port);

        /// Close it. Safe to call on one that was never opened.
        void close();

        /**
         * @brief Send @p size bytes to @p to.
         *
         * A datagram past MAX_DATAGRAM is refused rather than sent: it would be
         * fragmented, and one lost fragment loses all of it - which is a whole
         * packet lost for a reason the sender chose. The size and the cap are
         * both named in the log, because the caller's next question is by how
         * much they went over.
         *
         * @return False when there is no socket, nowhere to send, or it will
         *         not fit.
         */
        bool send(const NetAddress& to, const uint8_t* bytes, size_t size);

        /**
         * @brief Take the next datagram, if one is waiting.
         *
         * @param out  Filled with the payload; cleared first, so a false return
         *             never leaves the caller reading the packet before.
         * @param from Filled with where it came from.
         * @return False when nothing is waiting, which is the common case and
         *         not an error.
         */
        bool receive(std::vector<uint8_t>& out, NetAddress& from);

        /**
         * @brief The address this is bound to, which is how a caller learns the port
         * the system picked when it asked for zero.
         */
        NetAddress localAddress() const { return m_local; }

        bool isOpen() const { return m_handle != INVALID; }

    private:
        /**
         * @brief Say once that a datagram was too big to be ours, and drop it.
         *
         * @param bytes Its size, or zero where the platform refused the read
         *              without reporting one.
         */
        void noteOversized(size_t bytes);

    private:
        /**
         * @brief Sentinel for "no socket", which is -1 on POSIX and a large unsigned
         * value on Windows; held wide enough for both.
         */
        static constexpr intptr_t INVALID = -1;

    private:
        intptr_t   m_handle = INVALID;

        bool       m_readFailed  = false;  ///< A read failed; say so once, not per frame.
        bool       m_oversized   = false;  ///< One arrived too big; likewise, once.
        bool       m_writeFailed = false;  ///< The same, for a send.
        NetAddress m_local;
};

} // namespace Vkm::Engine
