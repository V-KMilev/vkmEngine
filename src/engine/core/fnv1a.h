#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Vkm::Engine {

// 64-bit FNV-1a: non-cryptographic, deterministic across runs and platforms.
constexpr uint64_t FNV1A_OFFSET_BASIS = 14695981039346656037ull;
constexpr uint64_t FNV1A_PRIME        = 1099511628211ull;

/**
 * @brief 64-bit FNV-1a of @p size bytes, continuing from @p seed.
 *
 * Not an overload of fnv1a64: fnv1a64("tag", seed) would bind the seed as a
 * byte count, and fnv1a64(buffer, size) the size as a seed.
 *
 * @param data Bytes to hash.
 * @param size How many.
 * @param seed A previous hash to continue, or the offset basis.
 * @return The hash.
 */
inline uint64_t fnv1a64Bytes(const void* data, std::size_t size, uint64_t seed = FNV1A_OFFSET_BASIS) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    uint64_t hash = seed;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= FNV1A_PRIME;
    }
    return hash;
}

/**
 * @brief 64-bit FNV-1a of a string's bytes, continuing from @p seed.
 *
 * @param s    Characters to hash; no terminator.
 * @param seed A previous hash to continue, or the offset basis.
 * @return The hash.
 */
inline uint64_t fnv1a64(std::string_view s, uint64_t seed = FNV1A_OFFSET_BASIS) {
    return fnv1a64Bytes(s.data(), s.size(), seed);
}

// 32-bit FNV-1a, for a hash that has to fit a 32-bit field.
constexpr uint32_t FNV1A32_OFFSET_BASIS = 2166136261u;
constexpr uint32_t FNV1A32_PRIME        = 16777619u;

/**
 * @brief 32-bit FNV-1a of a string's bytes, continuing from @p seed.
 *
 * @param s    Characters to hash; no terminator.
 * @param seed A previous hash to continue, or the offset basis.
 * @return The hash.
 */
inline uint32_t fnv1a32(std::string_view s, uint32_t seed = FNV1A32_OFFSET_BASIS) {
    uint32_t hash = seed;
    for (const char c : s) {
        hash ^= static_cast<unsigned char>(c);
        hash *= FNV1A32_PRIME;
    }
    return hash;
}

} // namespace Vkm::Engine
