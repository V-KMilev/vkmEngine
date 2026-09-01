#define VKM_LOG_CATEGORY "NET"

#include "net/transport/reliable.h"

#include <algorithm>

#include "logger.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Messages in one packet.
 *
 * Four bits, so a block can hold fifteen - far more than the budget allows anyway, and
 * the field costs nothing.
 */
constexpr uint32_t COUNT_BITS = 4;
constexpr uint32_t MAX_PER_PACKET = (1u << COUNT_BITS) - 1u;

/// Bits for a message's length. Enough for MAX_MESSAGE.
constexpr uint32_t SIZE_BITS = 12;

static_assert(NetReliable::MAX_MESSAGE < (1u << SIZE_BITS),
              "a message has to be able to say how long it is");

} // namespace

bool NetReliable::queue(const uint8_t* bytes, size_t size) {
    if (size == 0 || size > MAX_MESSAGE) {
        LOG_ERROR("A %zu byte message cannot be delivered; the limit is %zu",
                  size, MAX_MESSAGE);
        return false;
    }
    if (m_outgoing.size() >= MAX_QUEUED) {
        LOG_ERROR("%zu messages are unconfirmed; this peer has stopped listening",
                  m_outgoing.size());
        return false;
    }

    m_outgoing.push_back({m_nextId++, std::vector<uint8_t>(bytes, bytes + size)});
    return true;
}

void NetReliable::write(BitWriter& out) const {
    // What this end has taken, so the peer can stop repeating it. Written even
    // when there is nothing to send, because it is the only way the other
    // direction's queue ever empties.
    out.u32(m_taken);

    // Oldest first and only as many as the reservation holds. A message stays
    // at the front until it is confirmed, so nothing behind it can overtake it
    // and the receiver never has to hold a gap open.
    uint32_t written = 0;
    size_t   used    = 0;
    for (const Pending& message : m_outgoing) {
        if (written == MAX_PER_PACKET) break;
        if (used + message.bytes.size() > BUDGET_BYTES) break;
        used += message.bytes.size();
        ++written;
    }

    out.bits(written, COUNT_BITS);
    for (uint32_t i = 0; i < written; ++i) {
        const Pending& message = m_outgoing[i];
        out.u32(message.id);
        out.bits(static_cast<uint32_t>(message.bytes.size()), SIZE_BITS);
        out.append(message.bytes.data(), message.bytes.size() * 8u);
    }
}

bool NetReliable::read(BitReader& in, std::vector<std::vector<uint8_t>>& delivered) {
    const uint32_t confirmed = in.u32();
    const uint32_t count     = in.bits(COUNT_BITS);
    if (in.failed()) return false;

    // What the peer says it has taken. Dropping them here is the only thing
    // that ever shortens the queue.
    m_outgoing.erase(std::remove_if(m_outgoing.begin(), m_outgoing.end(),
                                    [confirmed](const Pending& message) {
                                        return message.id < confirmed;
                                    }),
                     m_outgoing.end());

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t id   = in.u32();
        const uint32_t size = in.bits(SIZE_BITS);
        if (in.failed() || size == 0 || size > MAX_MESSAGE) return false;

        std::vector<uint8_t> bytes(size);
        for (uint32_t byte = 0; byte < size; ++byte) {
            bytes[byte] = static_cast<uint8_t>(in.bits(8));
        }
        if (in.failed()) return false;

        // Anything but the one expected next is dropped. Below is a repeat the
        // peer had not heard was taken; above cannot happen while it sends
        // oldest-first, and taking it would deliver out of order.
        if (id != m_taken) continue;
        ++m_taken;
        delivered.push_back(std::move(bytes));
    }
    return true;
}

void NetReliable::clear() {
    m_outgoing.clear();
    m_nextId = 0;
    m_taken  = 0;
}

} // namespace Vkm::Engine
