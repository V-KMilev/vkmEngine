#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief Opaque handle pairing a sparse-array index with a generation counter.
 *
 * Valid only while its generation matches the slot's, so a stale handle is
 * detected. Index 0 is null.
 */
struct StorageIndex {
    uint32_t index      = 0;  ///< Sparse slot index; 0 is null.
    uint32_t generation = 0;  ///< The slot's generation when minted.

    constexpr explicit operator bool() const noexcept { return index != 0; }

    /**
     * @brief Compare two handles for identity.
     *
     * @param other Handle to compare against.
     * @return True if index and generation are both equal, so a stale handle
     *         never equals its recycled slot.
     */
    constexpr bool operator==(const StorageIndex& other) const noexcept {
        return index == other.index && generation == other.generation;
    }
    constexpr bool operator!=(const StorageIndex& other) const noexcept {
        return !(*this == other);
    }
};

static_assert(sizeof(StorageIndex) == 8, "StorageIndex must be 8 bytes");

} // namespace Vkm::Engine
