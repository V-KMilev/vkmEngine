#pragma once

#include <cstdint>
#include <vector>

#include "net/wire/bit_stream.h"

namespace Vkm::Engine {

/**
 * @brief Messages that must arrive, on a wire built to lose things.
 *
 * Almost nothing in this system needs delivering: state is superseded by the
 * next snapshot, input is carried a dozen times, and destruction is re-reported
 * until confirmed. What is left is the small set of things that happen once and
 * change what the world *is* rather than where it is - a body being created is
 * the reason this exists - for which there is no next packet that would say it
 * again.
 *
 * The cheapest thing that works: messages are numbered, the oldest unconfirmed
 * ones ride in every packet until the receiver says it has them, and the
 * receiver delivers them in order and exactly once. A sliding window with a
 * cumulative acknowledgement is the smallest shape giving all three at once, and
 * it costs nothing while the queue is empty, which is almost always.
 *
 * Cumulative on purpose - one number saying "I have everything below this". A
 * selective one would let a later message be delivered while an earlier one is
 * missing, and then order is a usual outcome rather than a guarantee.
 *
 * Not built on NetConnection's packet acknowledgement: that answers whether a
 * packet arrived, and a packet can arrive and be refused as stale. This tracks
 * what the receiver has taken.
 */
class NetReliable {
    public:
        NetReliable() = default;
        ~NetReliable() = default;

        NetReliable(const NetReliable& other) = delete;
        NetReliable& operator=(const NetReliable& other) = delete;

        NetReliable(NetReliable && other) = delete;
        NetReliable& operator=(NetReliable && other) = delete;

        /**
         * @brief Largest single message.
         *
         * Big enough for a prefab path and a pose, and small enough that one message
         * can never fail to fit a packet.
         */
        static constexpr size_t MAX_MESSAGE = 400;

        /**
         * @brief Room reserved in a packet for messages, taken before the world gets
         * what is left.
         *
         * A spawn a client never hears about is worse than one snapshot of slightly
         * stale positions.
         */
        static constexpr size_t BUDGET_BYTES = 420;

        /**
         * @brief Messages held unconfirmed before the queue is declared broken.
         *
         * A peer this far behind is not going to catch up.
         */
        static constexpr size_t MAX_QUEUED = 64;

    public:
        /**
         * @brief Queue @p bytes for delivery.
         *
         * @return False when the message is too large, or the queue is full
         *         because the peer has stopped confirming - in which case the
         *         caller has a connection to give up on, not a message to retry.
         */
        bool queue(const uint8_t* bytes, size_t size);

        /**
         * @brief Write the oldest unconfirmed messages that fit, and the number this
         * end has taken from the other direction.
         */
        void write(BitWriter& out) const;

        /**
         * @brief Read a packet's message block.
         *
         * @param in        Stream positioned at the block.
         * @param delivered Appended with messages not seen before, in order.
         * @return False when the block is malformed.
         */
        bool read(BitReader& in, std::vector<std::vector<uint8_t>>& delivered);

        /// How many are waiting. For the panel and for deciding a peer is lost.
        size_t pending() const { return m_outgoing.size(); }

        void clear();

    private:
        struct Pending {
            uint32_t             id = 0;
            std::vector<uint8_t> bytes;
        };

    private:
        std::vector<Pending> m_outgoing;      ///< Ascending by id, oldest first.
        uint32_t             m_nextId = 0;    ///< Number the next queued message takes.

        /**
         * @brief Everything below this has been taken from the peer, and is what this
         * end reports so the peer can stop sending it.
         */
        uint32_t m_taken = 0;
};

} // namespace Vkm::Engine
