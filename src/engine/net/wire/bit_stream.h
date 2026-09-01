#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief Writes values a bit at a time into a byte buffer.
 *
 * A packet has a hard ceiling and everything in it is competing for the same
 * room, so a field costs what it needs rather than what its type happens to be:
 * a flag is one bit, a player slot is four, a quantised coordinate is
 * twenty. Byte-aligned writing would round every one of those up to eight.
 *
 * Bounded by construction. Every write checks the room left and sets a sticky
 * overflow flag rather than growing, because the buffer's size is the packet's
 * size and a writer that grew would produce a datagram the socket then refuses.
 * A caller writes what it has, asks once whether it fit, and trims to the last
 * value that did.
 */
class BitWriter {
    public:
        /**
         * @brief Write into @p bytes, filling at most @p capacity of them.
         *
         * @param bytes    Buffer to fill; resized to the capacity and written in place.
         * @param capacity The packet's ceiling, in bytes.
         */
        BitWriter(std::vector<uint8_t>& bytes, size_t capacity)
            : m_bytes(bytes), m_capacity(capacity) {
            m_bytes.assign(capacity, 0u);
        }

        ~BitWriter() = default;

        BitWriter(const BitWriter& other) = delete;
        BitWriter& operator=(const BitWriter& other) = delete;

        BitWriter(BitWriter && other) = delete;
        BitWriter& operator=(BitWriter && other) = delete;

    public:
        /**
         * @brief Write the low @p width bits of @p value.
         *
         * @param value Value to write; bits above @p width are ignored.
         * @param width How many bits, 1 to 32.
         */
        void bits(uint32_t value, uint32_t width) {
            if (width == 0 || width > 32) return;
            if (m_bit + width > m_capacity * 8u) { m_overflowed = true; return; }

            for (uint32_t i = 0; i < width; ++i) {
                const uint32_t bit = (value >> i) & 1u;
                if (bit) m_bytes[(m_bit + i) >> 3] |= static_cast<uint8_t>(1u << ((m_bit + i) & 7u));
            }
            m_bit += width;
        }

        void boolean(bool value)  { bits(value ? 1u : 0u, 1); }
        void u8(uint8_t value)    { bits(value, 8); }
        void u16(uint16_t value)  { bits(value, 16); }
        void u32(uint32_t value)  { bits(value, 32); }

        /// Two words, high first, because the field width is capped at 32.
        void u64(uint64_t value) {
            bits(static_cast<uint32_t>(value >> 32), 32);
            bits(static_cast<uint32_t>(value), 32);
        }

        /// A float as its exact bits, for the values quantisation would spoil.
        void f32(float value) {
            uint32_t raw = 0;
            std::memcpy(&raw, &value, sizeof(raw));
            bits(raw, 32);
        }

        /**
         * @brief Write @p count bits from @p bytes, starting at its first bit.
         *
         * For splicing something already encoded into a larger packet. A
         * component is not a whole number of bytes, so it cannot be memcpy'd
         * into place - and padding each one to a byte boundary to allow that
         * would cost more than the entities dropped to make the room.
         *
         * @param bytes Buffer holding the encoded bits, first bit first.
         * @param count How many bits to take from it.
         */
        void append(const uint8_t* bytes, size_t count) {
            if (m_bit + count > m_capacity * 8u) { m_overflowed = true; return; }

            for (size_t i = 0; i < count; ++i) {
                if ((bytes[i >> 3] >> (i & 7u)) & 1u) {
                    m_bytes[(m_bit + i) >> 3] |= static_cast<uint8_t>(1u << ((m_bit + i) & 7u));
                }
            }
            m_bit += count;
        }

        /// Bits written so far, which is what a caller compares against a budget.
        size_t bitCount() const { return m_bit; }

        /// Whole bytes needed to carry what has been written.
        size_t byteCount() const { return (m_bit + 7u) >> 3; }

        /**
         * @brief True when a write did not fit.
         *
         * Sticky: one overflow makes every later question answer honestly, so a caller
         * tests once at the end.
         */
        bool overflowed() const { return m_overflowed; }

        /// Cut the buffer to what was actually written.
        void finish() { m_bytes.resize(byteCount()); }

    private:
        std::vector<uint8_t>& m_bytes;
        size_t m_capacity   = 0;
        size_t m_bit        = 0;
        bool   m_overflowed = false;
};

/**
 * @brief Reads back what a BitWriter wrote, in the same order.
 *
 * Sticky failure, like the writer's overflow: one read past the end fails every
 * read after it, so a decoder tests once when it is done rather than after
 * every field. A packet that has run out is refused whole rather than applied
 * in part, which is the only safe answer when the rest of it is unknown.
 */
class BitReader {
    public:
        BitReader(const uint8_t* bytes, size_t size) : m_bytes(bytes), m_size(size) {}

        ~BitReader() = default;

        BitReader(const BitReader& other) = delete;
        BitReader& operator=(const BitReader& other) = delete;

        BitReader(BitReader && other) = delete;
        BitReader& operator=(BitReader && other) = delete;

    public:
        uint32_t bits(uint32_t width) {
            if (width == 0 || width > 32) return 0;
            if (m_failed || m_bit + width > m_size * 8u) { m_failed = true; return 0; }

            uint32_t value = 0;
            for (uint32_t i = 0; i < width; ++i) {
                const size_t at = m_bit + i;
                if ((m_bytes[at >> 3] >> (at & 7u)) & 1u) value |= (1u << i);
            }
            m_bit += width;
            return value;
        }

        bool     boolean() { return bits(1) != 0; }
        uint8_t  u8()      { return static_cast<uint8_t>(bits(8)); }
        uint16_t u16()     { return static_cast<uint16_t>(bits(16)); }
        uint32_t u32()     { return bits(32); }

        /**
         * @brief Read back the pair u64 wrote, high half first.
         *
         * Into named locals rather than one expression, because the order an
         * expression's operands are evaluated in is unspecified - two reads in
         * one would be a wire format that differs between builds of the same
         * source.
         */
        uint64_t u64() {
            const uint64_t high = bits(32);
            const uint64_t low  = bits(32);
            return (high << 32) | low;
        }

        float f32() {
            const uint32_t raw = bits(32);
            float value = 0.0f;
            std::memcpy(&value, &raw, sizeof(value));
            return value;
        }

        /// True when a read ran past the end; sticky once set.
        bool failed() const { return m_failed; }

    private:
        const uint8_t* m_bytes = nullptr;
        size_t m_size   = 0;
        size_t m_bit    = 0;
        bool   m_failed = false;
};

} // namespace Vkm::Engine
