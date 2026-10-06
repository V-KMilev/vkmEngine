#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief Packs values to the bit into a byte buffer.
 *
 * A field costs what it needs, not its type's size: a flag is one bit.
 *
 * Bounded by the packet's size: a write past it sets a sticky overflow flag
 * instead of growing, so a caller asks once whether it all fit. Bits gather in
 * a 64-bit word; the last partial byte stays there until finish().
 */
class BitWriter {
    public:
        /**
         * @brief Write into @p bytes, filling at most @p capacity of them.
         *
         * @param bytes    Buffer to fill; emptied, then grown as bytes complete.
         *                 Holds the whole stream only after finish().
         * @param capacity The packet's ceiling, in bytes.
         */
        BitWriter(std::vector<uint8_t>& bytes, size_t capacity)
            : m_bytes(bytes)
            , m_capacity(capacity)
        {
            m_bytes.clear();
            m_bytes.reserve(capacity);
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
         * @param width 1 to 32.
         */
        void bits(uint32_t value, uint32_t width) {
            if (width == 0 || width > 32) return;
            if (m_bit + width > m_capacity * 8u) {
                m_overflowed = true;
                return;
            }

            const uint64_t field = static_cast<uint64_t>(value) & ((uint64_t(1) << width) - 1u);
            m_word     |= field << m_wordBits;
            m_wordBits += width;
            m_bit      += width;
            while (m_wordBits >= 8) {
                m_bytes.push_back(static_cast<uint8_t>(m_word));
                m_word     >>= 8;
                m_wordBits  -= 8;
            }
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
         * For splicing something already encoded, which is not a whole number
         * of bytes.
         *
         * @param bytes Buffer holding the encoded bits, first bit first.
         * @param count Bits to take from it.
         */
        void append(const uint8_t* bytes, size_t count) {
            if (m_bit + count > m_capacity * 8u) {
                m_overflowed = true;
                return;
            }

            // A byte at a time, as the source was laid down.
            size_t done = 0;
            for (; done + 8 <= count; done += 8) bits(bytes[done >> 3], 8);
            if (done < count) bits(bytes[done >> 3], static_cast<uint32_t>(count - done));
        }

        /// Bits written so far.
        size_t bitCount() const { return m_bit; }

        /// Whole bytes needed to carry what has been written.
        size_t byteCount() const { return (m_bit + 7u) >> 3; }

        /**
         * @brief True when a write did not fit.
         *
         * Sticky, so a caller tests once at the end.
         *
         * @return Whether any write since construction was refused.
         */
        bool overflowed() const { return m_overflowed; }

        /// Flush the last partial byte into the buffer.
        void finish() {
            if (m_wordBits > 0) m_bytes.push_back(static_cast<uint8_t>(m_word));
            m_word     = 0;
            m_wordBits = 0;
        }

    private:
        std::vector<uint8_t>& m_bytes;

        size_t   m_capacity   = 0;
        size_t   m_bit        = 0;
        uint64_t m_word       = 0;  ///< Bits not yet a whole byte, lowest first.
        uint32_t m_wordBits   = 0;
        bool     m_overflowed = false;
};

/**
 * @brief Reads back what a BitWriter wrote, in the same order.
 *
 * Failure is sticky, so a decoder tests once when it is done.
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
            if (m_failed || m_bit + width > m_size * 8u) {
                m_failed = true;
                return 0;
            }

            // The bound above keeps every byte loaded inside the buffer.
            while (m_wordBits < width) {
                m_word     |= static_cast<uint64_t>(m_bytes[m_next++]) << m_wordBits;
                m_wordBits += 8;
            }
            const uint32_t value = static_cast<uint32_t>(m_word & ((uint64_t(1) << width) - 1u));
            m_word     >>= width;
            m_wordBits  -= width;
            m_bit       += width;
            return value;
        }

        bool     boolean() { return bits(1) != 0; }
        uint8_t  u8()      { return static_cast<uint8_t>(bits(8)); }
        uint16_t u16()     { return static_cast<uint16_t>(bits(16)); }
        uint32_t u32()     { return bits(32); }

        /**
         * @brief Read back the pair u64 wrote, high half first.
         *
         * Into named locals: operand evaluation order is unspecified, so two
         * reads in one expression could differ between builds.
         *
         * @return The value; zero when the reader had already failed.
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

        size_t   m_size     = 0;
        size_t   m_bit      = 0;
        size_t   m_next     = 0;  ///< Next byte to load into the word.
        uint64_t m_word     = 0;  ///< Loaded bits not yet read, lowest first.
        uint32_t m_wordBits = 0;
        bool     m_failed   = false;
};

} // namespace Vkm::Engine
