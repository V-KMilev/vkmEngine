#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "net/wire/protocol.h"
#include "platform/net/net_address.h"
#include "platform/net/udp_socket.h"

namespace Vkm::Engine {

/**
 * @brief Is @p a a later sequence number than @p b?
 *
 * Sequences wrap: past half the space apart, older is the safe reading.
 *
 * @param a A sequence number.
 * @param b The one to compare it against.
 * @return True when @p a is the later of the two.
 */
bool isNewerSequence(uint16_t a, uint16_t b);

/**
 * @brief One end of one conversation: sequencing, acknowledgement and timing.
 *
 * Every packet carries its sequence, the newest seen the other way, and a
 * bitfield of the thirty-two before that.
 *
 * Nothing is retransmitted: the next packet says it again, newer.
 * Acknowledgement measures the trip and what a peer holds. A packet older than
 * one already seen is refused, or the world would mix two moments.
 */
class NetConnection {
    public:
        /**
         * @brief Bytes of header on every packet: sequence, acknowledgement, and the
         *        bitfield of the thirty-two before it.
         */
        static constexpr size_t HEADER_BYTES = 8;

        /// The most one packet carries past its header: what a datagram leaves.
        static constexpr size_t MAX_PAYLOAD = UdpSocket::MAX_DATAGRAM - HEADER_BYTES;

        /// How long silence lasts before a peer is given up on.
        static constexpr float TIMEOUT_SECONDS = 5.0f;

        /**
         * @brief The sequence number nothing ever sends, so a header can say "nothing".
         *
         * A sender skips it, on the first packet and on every wrap.
         */
        static constexpr uint16_t NO_SEQUENCE = 0;

    public:
        NetConnection() = default;
        ~NetConnection() = default;

        NetConnection(const NetConnection& other) = delete;
        NetConnection& operator=(const NetConnection& other) = delete;

        NetConnection(NetConnection && other) = delete;
        NetConnection& operator=(NetConnection && other) = delete;

    public:
        /// Point this at @p peer. Resets everything the last one left.
        void open(const NetAddress& peer);

        /**
         * @brief Wrap @p payload in a header and hand it back for sending.
         *
         * The caller owns the socket, which serves every peer of a server.
         *
         * @param payload What to carry; may be empty, which is a keep-alive.
         * @param size    Its length.
         * @param out     Filled with header followed by payload.
         */
        void frame(const uint8_t* payload, size_t size, std::vector<uint8_t>& out);

        /**
         * @brief Take a datagram that arrived from this peer.
         *
         * @param bytes   The whole datagram, header included.
         * @param size    Its length.
         * @param payload Filled with what rode inside it.
         * @param acknowledged Appended with the sequences this datagram
         *                confirms, each reported once only.
         * @return False when it is malformed, or older than one already seen.
         */
        bool accept(
            const uint8_t* bytes,
            size_t size,
            std::vector<uint8_t>& payload,
            std::vector<uint16_t>& acknowledged
        );

        /**
         * @brief Take back the acknowledgement for the packet just accepted.
         *
         * A sender reads an acknowledgement as the payload taken (see
         * NetBaseline::confirm), so one that would not decode is un-seen here
         * and described again.
         */
        void refuse();

        /// Age the silence timer and every unacknowledged packet by @p seconds of wall clock.
        void advance(float seconds);

        bool timedOut() const { return m_silent > TIMEOUT_SECONDS; }

        /**
         * @brief What frame() will stamp on the next packet.
         *
         * For a sender that must remember what that packet claimed.
         *
         * @return The next outgoing sequence.
         */
        uint16_t nextSequence() const { return m_outgoing; }

        /**
         * @brief Round trip, smoothed.
         *
         * @return Seconds; zero until an acknowledgement has been timed.
         */
        float roundTrip() const { return m_roundTrip; }

        /**
         * @brief What fraction of the peer's packets never arrived, so far.
         *
         * Counted from gaps in its sequence numbering; zero until something arrives.
         *
         * @return Lost over sent, in [0, 1).
         */
        float lossFraction() const {
            const uint64_t seen = m_arrived + m_missed;
            return seen == 0 ? 0.0f : static_cast<float>(m_missed) / static_cast<float>(seen);
        }

        /// How much that trip varies, smoothed the same way.
        float roundTripVariation() const { return m_variation; }

        const NetAddress& peer() const { return m_peer; }

    private:
        /**
         * @brief When one recent packet went out, to time its acknowledgement.
         */
        struct Sent {
            uint16_t sequence = 0;
            float    age      = 0.0f;
        };

    private:
        void recordTrip(uint16_t acknowledged, std::vector<uint16_t>& reported);

    private:
        NetAddress m_peer;

        uint16_t m_outgoing = NO_SEQUENCE + 1;  ///< Next sequence this end will send.
        uint16_t m_newest   = 0;                ///< Newest sequence seen from the peer.
        uint32_t m_seen     = 0;                ///< The thirty-two before it, one bit each.
        bool     m_heard    = false;

        uint16_t m_wasNewest = 0;  ///< That window before the last accept, for refuse().
        uint32_t m_wasSeen   = 0;
        bool     m_wasHeard  = false;

        float m_silent = 0.0f;  ///< Since anything arrived.

        uint64_t m_arrived = 0;  ///< Packets from the peer that landed.
        uint64_t m_missed  = 0;  ///< Gaps its sequence numbering stepped over.

        float m_roundTrip = 0.0f;
        float m_variation = 0.0f;

        std::vector<Sent> m_sent;
};

} // namespace Vkm::Engine
