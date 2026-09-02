#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "net/wire/protocol.h"
#include "platform/net/net_address.h"

namespace Vkm::Engine {

/**
 * @brief Is @p a a later sequence number than @p b?
 *
 * A distance question rather than a comparison, because sequences are sixteen
 * bits and wrap: past half the space apart the answer is unknowable, and older
 * is the safe reading. Free rather than a member, because a snapshot's own
 * acknowledgement bookkeeping asks it about numbers no connection holds.
 *
 * @param a A sequence number.
 * @param b The one to compare it against.
 * @return True when @p a is the later of the two.
 */
bool isNewerSequence(uint16_t a, uint16_t b);

/**
 * @brief One end of one conversation: sequencing, acknowledgement and timing.
 *
 * Every packet carries the sequence it is, the newest sequence seen coming the
 * other way, and a bitfield of the thirty-two before that. Three fields, and
 * between them each end learns which of its last thirty-three packets arrived -
 * for free, on traffic that was going out anyway, with nothing sent for the
 * purpose.
 *
 * Nothing is retransmitted. State is not worth resending because the packet
 * after it says the same thing more recently, and a command is not worth
 * resending because the packet after it carries a copy already. What
 * acknowledgement is for here is measuring the trip and knowing what a peer
 * holds, not repairing loss.
 *
 * A packet older than one already seen is refused. Out-of-order arrival is
 * indistinguishable from a stale duplicate at this level, and applying a frame
 * from before the one already applied is a world assembled from two moments.
 */
class NetConnection {
    public:
        /**
         * @brief Bytes of header on every packet: sequence, acknowledgement, and the
         * bitfield of the thirty-two before it.
         */
        static constexpr size_t HEADER_BYTES = 8;

        /// How long silence lasts before a peer is given up on.
        static constexpr float TIMEOUT_SECONDS = 5.0f;

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
         * The caller owns the socket, because one socket serves every peer a
         * server has and a connection pulling from it directly would take the
         * datagram belonging to the peer beside it.
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
         *                confirms, so a sender can commit what those packets
         *                claimed. Confirmed once each: a sequence already
         *                reported is not reported again, however many later
         *                packets keep acknowledging it.
         * @return False when it is malformed, or older than one already seen.
         */
        bool accept(const uint8_t* bytes, size_t size, std::vector<uint8_t>& payload,
                    std::vector<uint16_t>& acknowledged);

        /**
         * @brief Take back the acknowledgement for the packet just accepted.
         *
         * Acknowledgement is stamped when a datagram arrives, which is a claim
         * that it landed. A sender reads it as a claim that the payload was
         * taken: NetBaseline folds an acknowledged snapshot into what it believes
         * the other end holds and never describes those components again. So a
         * payload that would not decode is un-seen here, and the next packet
         * describes all of it rather than none of it.
         */
        void refuse();

        /// Move both silence timers on by @p seconds of wall clock.
        void advance(float seconds);

        /// True when this peer has said nothing for longer than the timeout.
        bool timedOut() const { return m_silent > TIMEOUT_SECONDS; }

        /**
         * @brief What frame() will stamp on the next packet.
         *
         * A sender that needs to know which packet its payload will ride in - to
         * remember what that packet claimed - asks before framing rather than guessing
         * after.
         */
        uint16_t nextSequence() const { return m_outgoing; }

        /**
         * @brief The sequence number nothing ever sends, so a header can say "nothing".
         *
         * Every packet carries the newest sequence its sender has heard, and a
         * sender that has heard nothing has to put something there. With every
         * value legal it wrote zero, which the far end could not tell from a
         * genuine acknowledgement of its own packet zero - and since the first
         * packet on a connection IS zero, a peer that had received nothing was
         * read as having received the first thing sent to it.
         *
         * Reserving one value is what makes "nothing" expressible. A sender
         * skips it, on the first packet and again on every wrap, so no real
         * packet ever carries it.
         */
        static constexpr uint16_t NO_SEQUENCE = 0;

        /**
         * @brief Round trip, smoothed.
         *
         * Half of this is what a command's trip costs, which is what a client's lead is
         * derived from.
         */
        float roundTrip() const { return m_roundTrip; }

        /**
         * @brief What fraction of the peer's packets never arrived, so far.
         *
         * Counted from the gaps in its sequence numbering rather than measured,
         * which is what makes it free: a packet that never came is exactly a
         * number the window stepped over. Zero until something has arrived.
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
        void recordTrip(uint16_t acknowledged, std::vector<uint16_t>& reported);

    private:
        /**
         * @brief When each recent packet went out, so an acknowledgement can be turned
         * into a duration.
         *
         * Bounded: a peer that never answers must not grow a list instead of being
         * noticed.
         */
        struct Sent {
            uint16_t sequence = 0;
            float    age      = 0.0f;
        };

    private:
        NetAddress m_peer;

        uint16_t m_outgoing = NO_SEQUENCE + 1;  ///< Next sequence this end will send.
        uint16_t m_newest   = 0;   ///< Newest sequence seen from the peer.
        uint32_t m_seen     = 0;   ///< The thirty-two before it, one bit each.
        bool     m_heard    = false;

        uint16_t m_wasNewest = 0;  ///< That window before the last accept, for refuse().
        uint32_t m_wasSeen   = 0;
        bool     m_wasHeard  = false;

        float m_silent = 0.0f;     ///< Since anything arrived.

        uint64_t m_arrived = 0;   ///< Packets from the peer that landed.
        uint64_t m_missed  = 0;   ///< Gaps its sequence numbering stepped over.

        float m_roundTrip = 0.0f;
        float m_variation = 0.0f;

        std::vector<Sent> m_sent;
};

} // namespace Vkm::Engine
