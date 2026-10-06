#include "net/transport/connection.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Vkm::Engine {

namespace {

/**
 * @brief How much of a new measurement is taken.
 *
 * Small, so one packet held in a router queue barely moves the estimate.
 */
constexpr float TRIP_SMOOTHING = 0.1f;

/// Half the sequence space: the point past which "newer" stops being knowable.
constexpr uint16_t SEQUENCE_HALF = 0x8000;

/**
 * @brief Packets remembered until acknowledged, so the acknowledgement can be timed.
 *
 * Bounded against a peer that never answers; anything older is not timed.
 */
constexpr size_t SENT_REMEMBERED = 256;

void writeU16(uint8_t* at, uint16_t value) {
    at[0] = static_cast<uint8_t>(value & 0xFF);
    at[1] = static_cast<uint8_t>(value >> 8);
}

uint16_t readU16(const uint8_t* at) {
    return static_cast<uint16_t>(at[0] | (at[1] << 8));
}

void writeU32(uint8_t* at, uint32_t value) {
    for (int i = 0; i < 4; ++i) at[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
}

uint32_t readU32(const uint8_t* at) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(at[i]) << (i * 8);
    return value;
}

} // namespace

bool isNewerSequence(uint16_t a, uint16_t b) {
    return (a > b && a - b < SEQUENCE_HALF) || (b > a && b - a > SEQUENCE_HALF);
}

void NetConnection::open(const NetAddress& peer) {
    m_peer      = peer;
    m_outgoing  = NO_SEQUENCE + 1;
    m_newest    = 0;
    m_seen      = 0;
    m_heard     = false;
    m_silent    = 0.0f;
    m_arrived   = 0;
    m_missed    = 0;
    m_roundTrip = 0.0f;
    m_variation = 0.0f;
    m_sent.clear();
}

void NetConnection::frame(const uint8_t* payload, size_t size, std::vector<uint8_t>& out) {
    out.clear();
    out.resize(HEADER_BYTES + size);

    const uint16_t sequence = m_outgoing++;
    // On the wrap as well as at the start: NO_SEQUENCE means "nothing heard".
    if (m_outgoing == NO_SEQUENCE) ++m_outgoing;
    writeU16(&out[0], sequence);
    writeU16(&out[2], m_newest);
    writeU32(&out[4], m_seen);
    if (size > 0) std::memcpy(&out[HEADER_BYTES], payload, size);

    m_sent.push_back({sequence, 0.0f});
    if (m_sent.size() > SENT_REMEMBERED) m_sent.erase(m_sent.begin());
}

bool NetConnection::accept(
    const uint8_t* bytes,
    size_t size,
    std::vector<uint8_t>& payload,
    std::vector<uint16_t>& acknowledged
) {
    if (size < HEADER_BYTES) return false;

    const uint16_t sequence     = readU16(&bytes[0]);
    const uint16_t acknowledgedNewest = readU16(&bytes[2]);
    const uint32_t acknowledgedBefore = readU32(&bytes[4]);

    if (m_heard && !isNewerSequence(sequence, m_newest)) return false;

    // Kept for refuse(): marking it seen claims a payload not yet read.
    m_wasNewest = m_newest;
    m_wasSeen   = m_seen;
    m_wasHeard  = m_heard;

    if (!m_heard) {
        m_newest = sequence;
        m_heard  = true;
    } else {
        // Shift and mark the old newest; at exactly 32 it is the last bit,
        // past that nothing is knowable. In 64 bits: a 32-bit shift by 32 is UB.
        const uint16_t advance = static_cast<uint16_t>(sequence - m_newest);

        m_missed += advance - 1;

        m_seen = advance > 32
            ? 0u
            : static_cast<uint32_t>(
                (static_cast<uint64_t>(m_seen) << advance) | (uint64_t{1} << (advance - 1))
            );
        m_newest = sequence;
    }

    ++m_arrived;

    // Nothing acknowledged, window included: timing or reporting it would
    // invent a trip and confirm a packet never received.
    if (acknowledgedNewest != NO_SEQUENCE) {
        recordTrip(acknowledgedNewest, acknowledged);
        for (int bit = 0; bit < 32; ++bit) {
            if (acknowledgedBefore & (1u << bit)) {
                recordTrip(static_cast<uint16_t>(acknowledgedNewest - (bit + 1)), acknowledged);
            }
        }
    }

    payload.assign(bytes + HEADER_BYTES, bytes + size);
    m_silent = 0.0f;
    return true;
}

void NetConnection::refuse() {
    m_newest = m_wasNewest;
    m_seen   = m_wasSeen;
    m_heard  = m_wasHeard;
}

void NetConnection::recordTrip(uint16_t acknowledged, std::vector<uint16_t>& reported) {
    const auto found = std::find_if(
        m_sent.begin(),
        m_sent.end(),
        [acknowledged](const Sent& s) { return s.sequence == acknowledged; }
    );
    // Not found: reported already, or fell out of the window.
    if (found == m_sent.end()) return;

    const float trip = found->age;
    m_sent.erase(found);
    reported.push_back(acknowledged);

    if (m_roundTrip <= 0.0f) {
        m_roundTrip = trip;
        m_variation = 0.0f;
        return;
    }
    const float error = trip - m_roundTrip;
    m_roundTrip += error * TRIP_SMOOTHING;
    m_variation += (std::abs(error) - m_variation) * TRIP_SMOOTHING;
}

void NetConnection::advance(float seconds) {
    m_silent += seconds;
    for (Sent& sent : m_sent) sent.age += seconds;
}

} // namespace Vkm::Engine
