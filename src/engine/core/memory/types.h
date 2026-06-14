#pragma once

#include <atomic>
#include <cstdint>

namespace Engine {

/**
 * @brief Opaque handle pairing a sparse-array index with a generation counter.
 *
 * A handle is only valid while its generation matches the slot's current generation,
 * preventing use-after-free and stale-handle bugs. Index 0 is reserved as the null sentinel.
 */
struct StorageIndex {
    uint32_t index      = 0; ///< Sparse slot index (0 = null/invalid)
    uint32_t generation = 0; ///< Generation at time of creation, compared against slot to detect staleness

    /// True if this is a valid/non-null index
    constexpr explicit operator bool() const noexcept { return index != 0; }

    /// Equality compares both index and generation
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

    /// Sets alive/dead status without changing generation
    void setAlive(bool alive) { value = (value & GEN_MASK) | (alive ? ALIVE_BIT : 0); }
    /// Increments generation, preserves alive state
    void bumpGeneration()     { value = (value & ALIVE_BIT) | (((value & GEN_MASK) + 1) & GEN_MASK); }

    /// @return True if alive bit is set
    bool alive()          const { return value & ALIVE_BIT; }
    /// @return The current generation (excluding alive bit)
    uint32_t generation() const { return value & GEN_MASK; }
};

static_assert(sizeof(GenerationIndex) == 4, "GenerationIndex must be 4 bytes");

/**
 * @brief Compile-time type-to-integer mapping for type-erased registries.
 *
 * Each unique type T gets a unique TypeId on first call to typeId<T>().
 * IDs are stable within a single program execution but not across runs.
 */
using TypeId = uint32_t;

namespace detail {
    inline TypeId nextTypeId() {
        static std::atomic<TypeId> counter{0};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }
}

template<typename T>
TypeId typeId() {
    static const TypeId id = detail::nextTypeId();
    return id;
}

} // namespace Engine
