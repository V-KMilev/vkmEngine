#pragma once

#include <cstdint>
#include <typeinfo>

namespace Vkm::Engine {

/**
 * @brief Opaque handle pairing a sparse-array index with a generation counter.
 *
 * Valid only while its generation matches the slot's, so a stale handle is
 * detected. Index 0 is null.
 */
struct StorageIndex {
    uint32_t index      = 0; ///< Sparse slot index; 0 is null.
    uint32_t generation = 0; ///< The slot's generation when minted.

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

/**
 * @brief Per-slot alive flag (bit 31) and generation counter (bits 0-30) in one uint32_t.
 */
struct GenerationIndex {
    uint32_t value = 0;

    static constexpr uint32_t ALIVE_BIT = uint32_t(1) << 31;
    static constexpr uint32_t GEN_MASK  = ~ALIVE_BIT;

    /**
     * @brief Set the alive/dead status, leaving the generation untouched.
     *
     * @param alive True marks the slot alive, false dead.
     */
    void setAlive(bool alive) { value = (value & GEN_MASK) | (alive ? ALIVE_BIT : 0); }

    /**
     * @brief Increment the generation counter, preserving the alive bit.
     *
     * Called on each removal, staling every handle minted before. Wraps at 31 bits.
     */
    void bumpGeneration() { value = (value & ALIVE_BIT) | (((value & GEN_MASK) + 1) & GEN_MASK); }

    bool     alive() const      { return value & ALIVE_BIT; }
    uint32_t generation() const { return value & GEN_MASK; }
};

static_assert(sizeof(GenerationIndex) == 4, "GenerationIndex must be 4 bytes");

/**
 * @brief Type-to-integer mapping for type-erased registries.
 *
 * Assigned on first typeId<T>() call; stable within a run, not across runs.
 * One registry keyed on the RTTI name (typeIdFromInfo), so hot-reloaded game code
 * agrees with the engine and an id outlives the module that first asked.
 */
using TypeId = uint32_t;

namespace detail {
    /**
     * @brief Stable id for @p info from the one process-wide registry in vkm_core.
     *
     * @param info Type to identify.
     * @return Its id, the same for every module.
     */
    TypeId typeIdFromInfo(const std::type_info& info);
}

template<typename T>
TypeId typeId() {
    // Cached per (module, T); the shared registry keeps the modules agreeing.
    static const TypeId s_id = detail::typeIdFromInfo(typeid(T));
    return s_id;
}

} // namespace Vkm::Engine
