#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Engine {

/**
 * @brief Stable 64-bit identity for an asset on disk.
 *
 * Generated once when a path is first imported (see AssetDatabase) and
 * persisted to assets/_database.json. Lets future scenes / components
 * reference assets by GUID instead of fragile path strings, so renaming
 * a texture file doesn't silently break every material that referenced
 * it.
 *
 * Value 0 is the invalid sentinel; valid GUIDs are nonzero.
 */
struct AssetId {
    uint64_t value = 0;

    constexpr bool isValid() const noexcept { return value != 0; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    /// 16-char zero-padded lowercase hex for JSON storage.
    std::string toString() const;

    /// Parses 16-char hex. Returns invalid AssetId on malformed input.
    static AssetId fromString(std::string_view s);

    /// Generate a fresh random GUID. Process-local thread_local PRNG
    /// seeded once from std::random_device.
    static AssetId generate();

    constexpr bool operator==(const AssetId& o) const noexcept { return value == o.value; }
    constexpr bool operator!=(const AssetId& o) const noexcept { return value != o.value; }
    constexpr bool operator< (const AssetId& o) const noexcept { return value <  o.value; }
};

} // namespace Engine

namespace std {

template<>
struct hash<Engine::AssetId> {
    size_t operator()(const Engine::AssetId& id) const noexcept {
        return hash<uint64_t>{}(id.value);
    }
};

} // namespace std
