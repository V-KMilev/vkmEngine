#include "net/transport/join_cookie.h"

#include <random>

namespace Vkm::Engine {

namespace {

uint64_t rotateLeft(uint64_t value, int bits) {
    return (value << bits) | (value >> (64 - bits));
}

void sipRound(uint64_t& v0, uint64_t& v1, uint64_t& v2, uint64_t& v3) {
    v0 += v1;
    v1 = rotateLeft(v1, 13);
    v1 ^= v0;
    v0 = rotateLeft(v0, 32);
    v2 += v3;
    v3 = rotateLeft(v3, 16);
    v3 ^= v2;
    v0 += v3;
    v3 = rotateLeft(v3, 21);
    v3 ^= v0;
    v2 += v1;
    v1 = rotateLeft(v1, 17);
    v1 ^= v2;
    v2 = rotateLeft(v2, 32);
}

/// Eight bytes as SipHash reads them, least significant first on every machine.
uint64_t readLittleEndian(const uint8_t* bytes) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
    return value;
}

uint64_t drawFromSystem() {
    std::random_device device;
    return (static_cast<uint64_t>(device()) << 32) | static_cast<uint64_t>(device());
}

} // namespace

uint64_t sipHash24(uint64_t k0, uint64_t k1, const uint8_t* data, size_t size) {
    uint64_t v0 = 0x736f6d6570736575ull ^ k0;
    uint64_t v1 = 0x646f72616e646f6dull ^ k1;
    uint64_t v2 = 0x6c7967656e657261ull ^ k0;
    uint64_t v3 = 0x7465646279746573ull ^ k1;

    const size_t whole = size - size % 8;
    for (size_t at = 0; at < whole; at += 8) {
        const uint64_t word = readLittleEndian(data + at);
        v3 ^= word;
        sipRound(v0, v1, v2, v3);
        sipRound(v0, v1, v2, v3);
        v0 ^= word;
    }

    // The tail, with the length's low byte in the top one.
    uint64_t last = static_cast<uint64_t>(size & 0xFFu) << 56;
    for (size_t at = whole; at < size; ++at) {
        last |= static_cast<uint64_t>(data[at]) << (8 * (at - whole));
    }
    v3 ^= last;
    sipRound(v0, v1, v2, v3);
    sipRound(v0, v1, v2, v3);
    v0 ^= last;

    v2 ^= 0xFFu;
    for (int round = 0; round < 4; ++round) sipRound(v0, v1, v2, v3);
    return v0 ^ v1 ^ v2 ^ v3;
}

void NetJoinCookie::rekey() {
    m_k0 = drawFromSystem();
    m_k1 = drawFromSystem();
}

uint64_t NetJoinCookie::issue(const NetAddress& to, double now) const {
    return forWindow(to, static_cast<uint64_t>(now / WINDOW_SECONDS));
}

bool NetJoinCookie::accepts(const NetAddress& from, uint64_t token, double now) const {
    const uint64_t window = static_cast<uint64_t>(now / WINDOW_SECONDS);
    return token == forWindow(from, window)
        || (window > 0 && token == forWindow(from, window - 1));
}

uint64_t NetJoinCookie::forWindow(const NetAddress& address, uint64_t window) const {
    // Byte by byte, so every machine hashes the same.
    uint8_t message[14];
    for (int i = 0; i < 4; ++i) message[i]     = static_cast<uint8_t>(address.ipv4 >> (8 * i));
    for (int i = 0; i < 2; ++i) message[4 + i] = static_cast<uint8_t>(address.port >> (8 * i));
    for (int i = 0; i < 8; ++i) message[6 + i] = static_cast<uint8_t>(window >> (8 * i));
    return sipHash24(m_k0, m_k1, message, sizeof(message));
}

} // namespace Vkm::Engine
