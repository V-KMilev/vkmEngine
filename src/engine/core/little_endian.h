#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Vkm::Engine {

/**
 * @brief An unsigned integer read from @p at, least significant byte first on every machine.
 *
 * @tparam T  The integer's type: its size is how many bytes are read.
 * @param  at The first byte; the caller has checked the rest are there.
 * @return The value.
 */
template <typename T>
constexpr T readLittleEndian(const uint8_t* at) {
    static_assert(std::is_unsigned_v<T>, "a little-endian read is of an unsigned integer");
    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i) value |= static_cast<T>(T(at[i]) << (8 * i));
    return value;
}

/**
 * @brief Write @p value to @p at, least significant byte first on every machine.
 *
 * @tparam T     The integer's type: its size is how many bytes are written.
 * @param  at    The first byte; the caller has room for sizeof(T).
 * @param  value The value.
 */
template <typename T>
constexpr void writeLittleEndian(uint8_t* at, T value) {
    static_assert(std::is_unsigned_v<T>, "a little-endian write is of an unsigned integer");
    for (size_t i = 0; i < sizeof(T); ++i) at[i] = static_cast<uint8_t>(value >> (8 * i));
}

} // namespace Vkm::Engine
