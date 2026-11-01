#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <istream>
#include <string>
#include <type_traits>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief Appends values to a byte buffer as they sit in memory: host-endian, nothing between them.
 *
 * The binary asset files are written through it - the cooked files and the .terrain body - and
 * read back by ByteReader.
 */
class ByteWriter {
    public:
        explicit ByteWriter(std::vector<uint8_t>& out) : m_out(out) {}
        ~ByteWriter() = default;

        ByteWriter(const ByteWriter& other) = delete;
        ByteWriter& operator=(const ByteWriter& other) = delete;

        ByteWriter(ByteWriter && other) = delete;
        ByteWriter& operator=(ByteWriter && other) = delete;

    public:
        template<typename T>
        void raw(const T& value) {
            static_assert(std::is_trivially_copyable_v<T>, "a value is written as it sits in memory");
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            m_out.insert(m_out.end(), bytes, bytes + sizeof(T));
        }

        template<typename T>
        void bulk(const std::vector<T>& values) {
            static_assert(std::is_trivially_copyable_v<T>, "a value is written as it sits in memory");
            const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
            m_out.insert(m_out.end(), bytes, bytes + values.size() * sizeof(T));
        }

        void chars(const std::string& value) { m_out.insert(m_out.end(), value.begin(), value.end()); }

    private:
        std::vector<uint8_t>& m_out;
};

/**
 * @brief Takes values off a run of bytes as ByteWriter put them there.
 *
 * Each read refuses, returning false, a value past the run's end, so a count read from the run
 * cannot ask for more than it holds, however large it is. The run is bytes in memory, or the next
 * bytes of a stream, read straight into the values: a cooked file's length is checked against its
 * header first, so its body need not be read whole to be bounded.
 */
class ByteReader {
    public:
        ByteReader(const uint8_t* bytes, size_t size) : m_at(bytes), m_left(size) {}
        ByteReader(std::istream& in, uint64_t size) : m_in(&in), m_left(size) {}
        ~ByteReader() = default;

        ByteReader(const ByteReader& other) = delete;
        ByteReader& operator=(const ByteReader& other) = delete;

        ByteReader(ByteReader && other) = delete;
        ByteReader& operator=(ByteReader && other) = delete;

    public:
        template<typename T>
        bool raw(T& value) {
            static_assert(std::is_trivially_copyable_v<T>, "a value is read as it sits in memory");
            return take(&value, sizeof(T));
        }

        template<typename T>
        bool bulk(std::vector<T>& values, uint64_t count) {
            static_assert(std::is_trivially_copyable_v<T>, "a value is read as it sits in memory");
            if (count > m_left / sizeof(T)) return false;
            values.resize(static_cast<size_t>(count));
            return take(values.data(), values.size() * sizeof(T));
        }

        bool chars(std::string& value, uint64_t count) {
            if (count > m_left) return false;
            value.resize(static_cast<size_t>(count));
            return take(value.data(), value.size());
        }

        uint64_t left() const { return m_left; }

    private:
        // A stream that fails to give what its length promised has nothing more to give.
        bool take(void* to, size_t bytes) {
            if (bytes > m_left) return false;
            if (bytes == 0) return true;
            if (m_in) {
                if (!m_in->read(static_cast<char*>(to), static_cast<std::streamsize>(bytes))) {
                    m_left = 0;
                    return false;
                }
            } else {
                std::memcpy(to, m_at, bytes);
                m_at += bytes;
            }
            m_left -= bytes;
            return true;
        }

    private:
        const uint8_t* m_at   = nullptr;  ///< The next byte, for a run in memory.
        std::istream*  m_in   = nullptr;  ///< The stream, for a run read off one.
        uint64_t       m_left = 0;
};

} // namespace Vkm::Engine
