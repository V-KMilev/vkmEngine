#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "net/wire/bit_stream.h"

namespace Vkm::Engine {

/// Longest message a game can send: its length goes on the wire in a byte.
constexpr size_t NET_MESSAGE_MAX_BYTES = 255;

/**
 * @brief Most messages a sender holds unacknowledged; a send past it is refused.
 *
 * Small against the sixteen-bit sequence, so a cumulative acknowledgement is never ambiguous.
 */
constexpr size_t NET_MESSAGE_WINDOW = 64;

/**
 * @brief The sending half of messages that must arrive: once each, in the order sent.
 *
 * A message is numbered and kept until the other end says it has it. Every packet carries
 * every message still kept that fits, oldest first - the commands' answer to loss, not a
 * retransmit timer - so a message rides each packet sent until one arrives, and the receiver
 * drops the copies it already has.
 */
class NetOutbox {
    public:
        /// What write() writes when no message fits: the bit saying none follow.
        static constexpr uint32_t NONE_BITS = 1;

    public:
        NetOutbox() = default;
        ~NetOutbox() = default;

        NetOutbox(const NetOutbox& other) = default;
        NetOutbox& operator=(const NetOutbox& other) = default;

        NetOutbox(NetOutbox && other) = default;
        NetOutbox& operator=(NetOutbox && other) = default;

    public:
        /**
         * @brief Keep @p size bytes to send.
         *
         * @param bytes The message.
         * @param size  Its length, 1 to NET_MESSAGE_MAX_BYTES.
         * @return False, keeping nothing, when it is empty or too long, or NET_MESSAGE_WINDOW are waiting.
         */
        bool push(const uint8_t* bytes, size_t size);

        /**
         * @brief Write the oldest messages kept that fit in @p budgetBits.
         *
         * Always at least the one bit saying whether any follow.
         *
         * @param out        The packet.
         * @param budgetBits Room left in it.
         * @return How many were written.
         */
        size_t write(BitWriter& out, size_t budgetBits) const;

        /**
         * @brief The other end has every message up to @p newest: stop sending them.
         *
         * @param newest The latest sequence it has, as its acknowledgement says.
         */
        void acknowledge(uint16_t newest);

        /// Messages waiting to be acknowledged.
        size_t pending() const { return m_pending.size(); }

        /// Forget every message, and number the next one from the start.
        void clear();

    private:
        struct Message {
            uint16_t             sequence = 0;
            std::vector<uint8_t> bytes;
        };

    private:
        std::deque<Message> m_pending;
        uint16_t            m_next = 1;  ///< The next message's sequence; zero means none yet.
};

/**
 * @brief The receiving half: each message handed on once, in the order it was sent.
 */
class NetInbox {
    public:
        using Deliver = std::function<void(const uint8_t* bytes, size_t size)>;

    public:
        NetInbox() = default;
        ~NetInbox() = default;

        NetInbox(const NetInbox& other) = default;
        NetInbox& operator=(const NetInbox& other) = default;

        NetInbox(NetInbox && other) = default;
        NetInbox& operator=(NetInbox && other) = default;

    public:
        /**
         * @brief Read a packet's messages, handing on those not seen before.
         *
         * A copy of one already handed on is skipped; one past a gap is not handed on, and is
         * said again by the sender, which only ever starts from what is unacknowledged. At most
         * NET_MESSAGE_WINDOW are handed on between two acknowledgements, as many as an honest
         * sender can have waiting: one past them is left for the sender to say again, so a peer
         * sending packet after packet of messages costs the game no more than one keeping up.
         *
         * @param in      The packet, at its messages.
         * @param deliver Called for each new message, in order.
         * @return False when the messages did not decode.
         */
        bool read(BitReader& in, const Deliver& deliver);

        /// The latest sequence handed on, for the acknowledgement; zero before any.
        uint16_t newest() const { return m_newest; }

        /// newest(), as an acknowledgement sent: the sender may now be handed on as many again.
        uint16_t acknowledge() {
            m_room = NET_MESSAGE_WINDOW;
            return m_newest;
        }

    private:
        uint16_t m_newest = 0;
        size_t   m_room   = NET_MESSAGE_WINDOW;  ///< Still to hand on before the next acknowledgement.
};

} // namespace Vkm::Engine
