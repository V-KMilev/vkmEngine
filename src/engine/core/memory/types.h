#pragma once

#include <cstdint>
#include <typeinfo>

namespace Vkm::Engine {

/**
 * @brief Opaque handle pairing a sparse-array index with a generation counter.
 *
 * A handle is only valid while its generation matches the slot's current generation,
 * preventing use-after-free and stale-handle bugs. Index 0 is reserved as the null sentinel.
 */
struct StorageIndex {
    uint32_t index      = 0; ///< Sparse slot index (0 = null/invalid)
    uint32_t generation = 0; ///< Generation at time of creation, compared against slot to detect staleness

    /**
     * @brief Test whether this handle refers to a real slot.
     *
     * @return True when index is non-zero (0 is the reserved null sentinel).
     */
    constexpr explicit operator bool() const noexcept { return index != 0; }

    /**
     * @brief Compare two handles for identity.
     *
     * Both the slot index and the generation must match, so a recycled slot with
     * a bumped generation never compares equal to a stale handle.
     *
     * @param other Handle to compare against.
     * @return True if index and generation are both equal.
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
 * @brief Bit-packed per-slot metadata: alive flag + generation counter in a single uint32_t.
 *
 * Bit 31 (MSB) stores the alive/dead state. Bits 0-30 store a monotonically
 * increasing generation counter that is bumped on each removal.
 */
struct GenerationIndex {
    uint32_t value = 0;

    static constexpr uint32_t ALIVE_BIT = uint32_t(1) << 31; ///< Bit 31: 1 = alive, 0 = dead
    static constexpr uint32_t GEN_MASK  = ~ALIVE_BIT;        ///< Bits 0-30: generation counter

    /**
     * @brief Set the alive/dead status, leaving the generation untouched.
     *
     * @param alive True marks the slot live, false marks it dead.
     */
    void setAlive(bool alive) { value = (value & GEN_MASK) | (alive ? ALIVE_BIT : 0); }
    /**
     * @brief Increment the generation counter, preserving the alive bit.
     *
     * Called on each removal so any handle minted against the old generation is
     * detected as stale. Wraps within the 31-bit GEN_MASK.
     */
    void bumpGeneration()     { value = (value & ALIVE_BIT) | (((value & GEN_MASK) + 1) & GEN_MASK); }

    /**
     * @brief Query the alive flag.
     *
     * @return True when the alive bit is set.
     */
    bool alive()          const { return value & ALIVE_BIT; }
    /**
     * @brief Query the current generation counter.
     *
     * @return The generation value with the alive bit masked off.
     */
    uint32_t generation() const { return value & GEN_MASK; }
};

static_assert(sizeof(GenerationIndex) == 4, "GenerationIndex must be 4 bytes");

/**
 * @brief Type-to-integer mapping for type-erased registries.
 *
 * Each unique type T gets a unique TypeId on first call to typeId<T>(). IDs are
 * stable within a single program execution but not across runs.
 *
 * The id is resolved through a single RTTI-keyed registry (typeIdFromInfo,
 * defined once in vkm_core) rather than a per-template counter, so the same
 * type maps to the same id across module boundaries - required so hot-reloaded
 * game code in a separate DLL agrees with the engine on component / event /
 * resource type ids.
 */
using TypeId = uint32_t;

namespace detail {
    /**
     * @brief Stable id for @p info from one process-wide registry (single definition
     * in vkm_core - one instance even when vkm_core is a shared library).
     */
    TypeId typeIdFromInfo(const std::type_info& info);
}

template<typename T>
TypeId typeId() {
    // Cached per (module, T); the value comes from the shared registry, so all
    // modules agree even though each caches its own local.
    static const TypeId id = detail::typeIdFromInfo(typeid(T));
    return id;
}

} // namespace Vkm::Engine
