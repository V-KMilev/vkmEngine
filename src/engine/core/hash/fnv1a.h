#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Vkm::Engine {

// 64-bit FNV-1a. Non-cryptographic content hash used to key cooked assets to
// their recipe (and the cooker version). Speed + zero dependency is the point;
// collision resistance is irrelevant here. Deterministic across runs/platforms.
constexpr uint64_t FNV1A_OFFSET_BASIS = 14695981039346656037ull;
constexpr uint64_t FNV1A_PRIME        = 1099511628211ull;

// Hash `size` bytes, optionally continuing from a previous hash via `seed` so
// callers can fold several inputs together (e.g. recipe bytes then the cooker
// version): fnv1a64(&version, sizeof(version), fnv1a64(recipeBytes)).
inline uint64_t fnv1a64(const void* data, std::size_t size, uint64_t seed = FNV1A_OFFSET_BASIS) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    uint64_t hash = seed;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= FNV1A_PRIME;
    }
    return hash;
}

inline uint64_t fnv1a64(std::string_view s, uint64_t seed = FNV1A_OFFSET_BASIS) {
    return fnv1a64(s.data(), s.size(), seed);
}

// 32-bit FNV-1a, for the one place a hash has to fit a 32-bit field: the wire's
// schema fingerprint, which both ends compare at join. Same construction, same
// determinism, half the width.
constexpr uint32_t FNV1A32_OFFSET_BASIS = 2166136261u;
constexpr uint32_t FNV1A32_PRIME        = 16777619u;

inline uint32_t fnv1a32(std::string_view s, uint32_t seed = FNV1A32_OFFSET_BASIS) {
    uint32_t hash = seed;
    for (const char c : s) {
        hash ^= static_cast<unsigned char>(c);
        hash *= FNV1A32_PRIME;
    }
    return hash;
}

} // namespace Vkm::Engine
