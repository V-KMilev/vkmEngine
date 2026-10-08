#include "net/transport/messages.h"

#include "net/transport/connection.h"

namespace Vkm::Engine {

namespace {

constexpr uint32_t COUNT_BITS  = 7;   ///< Messages in one packet.
constexpr uint32_t LENGTH_BITS = 8;   ///< A message's bytes.
constexpr uint32_t HEADER_BITS = NetOutbox::NONE_BITS + 16 + COUNT_BITS;
constexpr size_t   MAX_PER_PACKET = (1u << COUNT_BITS) - 1;

static_assert(NET_MESSAGE_MAX_BYTES < (1u << LENGTH_BITS), "a message's length has to fit its field");
static_assert(NET_MESSAGE_WINDOW < 0x8000, "a cumulative acknowledgement must not be read as older");
static_assert(MAX_PER_PACKET >= NET_MESSAGE_WINDOW, "a packet has to be able to carry every message waiting");

/// The sequence after @p sequence, both ends: zero means "none yet", so the count steps over it.
uint16_t nextSequence(uint16_t sequence) {
    const uint16_t next = static_cast<uint16_t>(sequence + 1);
    return next == 0 ? 1 : next;
}

} // namespace

bool NetOutbox::push(const uint8_t* bytes, size_t size) {
    const bool fits = bytes && size > 0 && size <= NET_MESSAGE_MAX_BYTES;
    if (!fits || m_pending.size() >= NET_MESSAGE_WINDOW) return false;
    m_pending.push_back({m_next, std::vector<uint8_t>(bytes, bytes + size)});
    m_next = nextSequence(m_next);
    return true;
}

size_t NetOutbox::write(BitWriter& out, size_t budgetBits) const {
    // How many fit, decided before a bit is written, so the count leads them.
    size_t count = 0;
    size_t used  = HEADER_BITS;
    for (const Message& message : m_pending) {
        const size_t cost = LENGTH_BITS + 8u * message.bytes.size();
        if (count == MAX_PER_PACKET || used + cost > budgetBits) break;
        used += cost;
        ++count;
    }

    out.boolean(count > 0);
    if (count == 0) return 0;
    out.u16(m_pending.front().sequence);
    out.bits(static_cast<uint32_t>(count), COUNT_BITS);
    for (size_t i = 0; i < count; ++i) {
        const Message& message = m_pending[i];
        out.bits(static_cast<uint32_t>(message.bytes.size()), LENGTH_BITS);
        for (const uint8_t byte : message.bytes) out.u8(byte);
    }
    return count;
}

void NetOutbox::acknowledge(uint16_t newest) {
    while (!m_pending.empty()) {
        const uint16_t front = m_pending.front().sequence;
        if (front != newest && isNewerSequence(front, newest)) break;
        m_pending.pop_front();
    }
}

void NetOutbox::clear() {
    m_pending.clear();
    m_next = 1;
}

bool NetInbox::read(BitReader& in, const Deliver& deliver) {
    if (!in.boolean()) return !in.failed();
    uint16_t       sequence = in.u16();
    const uint32_t count    = in.bits(COUNT_BITS);
    if (in.failed()) return false;

    std::vector<uint8_t> bytes;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t size = in.bits(LENGTH_BITS);
        if (in.failed() || size == 0) return false;
        bytes.resize(size);
        for (uint32_t b = 0; b < size; ++b) bytes[b] = in.u8();
        if (in.failed()) return false;

        // The next one after the newest handed on, and only that one: a copy is behind it, and
        // one past a gap would arrive out of order.
        if (sequence == nextSequence(m_newest) && m_room > 0) {
            m_newest = sequence;
            --m_room;
            deliver(bytes.data(), bytes.size());
        }
        sequence = nextSequence(sequence);
    }
    return true;
}

} // namespace Vkm::Engine
