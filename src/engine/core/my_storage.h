#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <type_traits>

#include "l_assert.h"

namespace Engine {
/**
* @brief Opaque handle returned by Storage::add().
*
* Pairs a sparse-array index with a generation counter. A handle is only valid
* while its generation matches the slot's current generation, preventing
* use-after-free and stale-handle bugs. Index 0 is reserved as the null sentinel.
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
* @class Storage
* @brief Generational arena (slot map) with O(1) add, remove, and lookup by stable handle.
*
* Elements are stored in a packed dense array for cache-friendly O(n) iteration.
* A parallel sparse array maps handles to dense positions, with dead slots forming
* an intrusive free list for O(1) recycling. Generation counters on each slot
* ensure stale handles are detected rather than silently aliasing reused slots.
*
* Uses memcpy for trivially-copyable types during swap-and-pop removal,
* falling back to move-assignment otherwise.
*
* @tparam T Element type to store
*/
template<typename T>
class Storage {
    public:
        /**
        * @brief Constructs an empty storage container with the 0th slot reserved.
        */
        Storage() :
            m_generation({GenerationIndex{}}),
            m_dataIndex({0}),
            m_dataId(),
            m_data(),
            m_freeHead(0) {}

        ~Storage() = default;

        Storage(const Storage& other) = delete;
        Storage& operator=(const Storage& other) = delete;

        Storage(Storage && other) = delete;
        Storage& operator=(Storage && other) = delete;

    public:
        /**
        * @brief Insert an element into the storage.
        * @param value Element to insert (moved or copied).
        * @return Stable handle that remains valid until the element is removed.
        */
        StorageIndex add(T && value)     { return addInternal(std::move(value)); }
        StorageIndex add(const T& value) { return addInternal(value); }

        /**
        * @brief Remove the element identified by @p key, recycling its sparse slot.
        * @param key Handle to the element to remove. Must be valid (asserts).
        *
        * Swap-and-pops the dense array to keep it packed, then pushes the
        * freed sparse slot onto the free list with a bumped generation.
        */
        void remove(StorageIndex key) {
            VKM_ASSERT(has(key), "Storage::remove called with invalid key");

            uint32_t dataIdx = m_dataIndex[key.index];
            uint32_t lastIdx = static_cast<uint32_t>(m_data.size() - 1);

            // Swap-and-pop: move the last dense element into the hole
            if (dataIdx != lastIdx) {
                if constexpr (std::is_trivially_copyable_v<T>) {
                    std::memcpy(&m_data[dataIdx], &m_data[lastIdx], sizeof(T));
                } else {
                    m_data[dataIdx] = std::move(m_data[lastIdx]);
                }

                // Update reverse and forward mappings for moved element
                m_dataId[dataIdx]              = m_dataId[lastIdx];
                m_dataIndex[m_dataId[dataIdx]] = dataIdx;
            }

            m_data.pop_back();
            m_dataId.pop_back();

            // Recycle the sparse slot: bump generation, mark dead, push onto free list
            m_generation[key.index].bumpGeneration();
            m_generation[key.index].setAlive(false);
            m_dataIndex[key.index] = m_freeHead;
            m_freeHead = key.index;
        }

        /**
        * @brief Access the element identified by @p key.
        * @param key Handle to look up. Must be valid (asserts).
        * @return Reference to the stored element.
        */
        T&       get(StorageIndex key)       { VKM_ASSERT(has(key), "Storage::get called with invalid key"); return m_data[m_dataIndex[key.index]]; }
        const T& get(StorageIndex key) const { VKM_ASSERT(has(key), "Storage::get called with invalid key"); return m_data[m_dataIndex[key.index]]; }

        /**
        * @brief Test whether @p key refers to a live element.
        * @param key Handle to validate.
        * @return True if the slot is alive and the generation matches.
        */
        bool has(StorageIndex key) const {
            return key
                && key.index < m_generation.size()
                && m_generation[key.index].alive()
                && m_generation[key.index].generation() == key.generation;
        }

    public:
        /**
        * @brief Pre-allocate memory for all internal arrays.
        * @param capacity Expected number of elements. Avoids reallocations during repeated add().
        */
        void reserve(size_t capacity) {
            m_generation.reserve(capacity);
            m_dataIndex.reserve(capacity);
            m_dataId.reserve(capacity);
            m_data.reserve(capacity);
        }

        /** @brief Remove all elements and reset to initial state. Allocated capacity is retained. */
        void clear() {
            m_generation.clear();
            m_dataIndex.clear();
            m_dataId.clear();
            m_data.clear();

            m_dataIndex.push_back(0);
            m_generation.push_back({});
            m_freeHead = 0;
        }

        size_t size() const { return m_data.size(); }  ///< Number of live elements.
        bool empty()  const { return m_data.empty(); } ///< True if size() == 0.

        T*       data()       { return m_data.data(); } ///< Raw pointer to the packed dense array.
        const T* data() const { return m_data.data(); } ///< @copydoc data()

        /**
        * @brief Return the StorageIndex (handle) for the element at a given dense array position.
        * @param denseIndex Position in the packed data array (must be < size()).
        * @return The StorageIndex that maps to this element.
        */
        StorageIndex keyAt(uint32_t denseIndex) const {
            VKM_ASSERT(denseIndex < m_data.size(), "Storage::keyAt index out of bounds");
            uint32_t sparseIdx = m_dataId[denseIndex];
            return StorageIndex{sparseIdx, m_generation[sparseIdx].generation()};
        }

        /// @name Range-based for loop support (iterates the packed dense array, no holes).
        /// @{
        auto begin()       { return m_data.begin(); }
        auto begin() const { return m_data.begin(); }
        auto end()         { return m_data.end(); }
        auto end()   const { return m_data.end(); }
        /// @}

    private:
        /// @brief Allocates a sparse slot, emplaces into the dense array, and wires up both mappings.
        template<typename... Args>
        StorageIndex addInternal(Args&&... args) {
            uint32_t idx     = allocateSlot();
            uint32_t dataIdx = static_cast<uint32_t>(m_data.size());

            m_data.emplace_back(std::forward<Args>(args)...);
            m_dataId.push_back(idx);

            m_dataIndex[idx] = dataIdx;
            m_generation[idx].setAlive(true);

            return StorageIndex{idx, m_generation[idx].generation()};
        }

        /// @brief Pop a slot from the free list, or grow the sparse arrays if empty.
        uint32_t allocateSlot() {
            if (m_freeHead != 0) {
                uint32_t idx = m_freeHead;
                m_freeHead = m_dataIndex[idx];
                return idx;
            }

            uint32_t idx = static_cast<uint32_t>(m_dataIndex.size());
            m_dataIndex.push_back(0);
            m_generation.push_back({});
            return idx;
        }

    private:
        std::vector<GenerationIndex> m_generation;  ///< Sparse: alive + generation per slot
        std::vector<uint32_t> m_dataIndex;          ///< Sparse-to-dense mapping (free-list link when dead)
        std::vector<uint32_t> m_dataId;             ///< Dense-to-sparse reverse mapping
        std::vector<T> m_data;                      ///< Dense: packed element storage

        uint32_t m_freeHead;                        ///< Head of intrusive free list (0 = empty)
};

} // namespace Engine
