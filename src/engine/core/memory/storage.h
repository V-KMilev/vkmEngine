#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <type_traits>

#include "l_assert.h"
#include "core/memory/types.h"

namespace Engine {

/**
* @class IStorage
* @brief Type-erased interface for Storage, enabling heterogeneous storage in registries.
*/
class IStorage {
    public:
        virtual ~IStorage() = default;
        virtual void remove(StorageIndex key) = 0;
        virtual bool has(StorageIndex key) const = 0;
        virtual size_t size() const = 0;
        virtual uint64_t typeVersion() const = 0;
};

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
class Storage : public IStorage {
    public:
        /**
        * @brief Constructs an empty storage container with the 0th slot reserved.
        */
        Storage() :
            m_generation({GenerationIndex{}}),
            m_dataIndex({0}),
            m_refCounts({0}),
            m_dataId({}),
            m_data({}),
            m_freeHead(0) {}

        ~Storage() override = default;

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
        * @brief Increment the reference count for the element at @p key.
        * @param key Handle to the element. Must be valid (asserts).
        */
        void acquire(StorageIndex key) {
            VKM_ASSERT(has(key), "Storage::acquire called with invalid key");
            ++m_refCounts[key.index];
        }

        /**
        * @brief Decrement the reference count for the element at @p key.
        * @param key Handle to the element. Must be valid and have refCount > 0 (asserts).
        */
        void release(StorageIndex key) {
            VKM_ASSERT(has(key), "Storage::release called with invalid key");
            VKM_ASSERT(m_refCounts[key.index] > 0, "Storage::release called with zero refCount");
            --m_refCounts[key.index];
        }

        /**
        * @brief Query the current reference count for the element at @p key.
        * @param key Handle to the element. Must be valid (asserts).
        * @return Current reference count (0 means safe to remove).
        */
        uint32_t refCount(StorageIndex key) const {
            VKM_ASSERT(has(key), "Storage::refCount called with invalid key");
            return m_refCounts[key.index];
        }

        /**
        * @brief Remove the element identified by @p key, recycling its sparse slot.
        * @param key Handle to the element to remove. Must be valid (asserts).
        *
        * Warns if the element has a non-zero reference count.
        * Swap-and-pops the dense array to keep it packed, then pushes the
        * freed sparse slot onto the free list with a bumped generation.
        */
        void remove(StorageIndex key) override {
            VKM_ASSERT(has(key), "Storage::remove called with invalid key");
            VKM_ASSERT(m_refCounts[key.index] == 0,
                "Storage::remove called on element with non-zero refCount (%u)", m_refCounts[key.index]);

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
        bool has(StorageIndex key) const override {
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
            m_refCounts.reserve(capacity);
            m_dataId.reserve(capacity);
            m_data.reserve(capacity);
        }

        /** @brief Remove all elements and reset to initial state. Allocated capacity is retained. */
        void clear() {
            m_generation.clear();
            m_dataIndex.clear();
            m_refCounts.clear();
            m_dataId.clear();
            m_data.clear();

            m_dataIndex.push_back(0);
            m_refCounts.push_back(0);
            m_generation.push_back({});
            m_freeHead = 0;
        }

        size_t size() const override { return m_data.size(); }  ///< Number of live elements.
        bool empty()  const { return m_data.empty(); } ///< True if size() == 0.

        T*       data()       { return m_data.data(); } ///< Raw pointer to the packed dense array.
        const T* data() const { return m_data.data(); } ///< @copydoc data()

        /// @name Range-based for loop support (iterates the packed dense array, no holes).
        /// @{
        auto begin()       { return m_data.begin(); }
        auto begin() const { return m_data.begin(); }
        auto end()         { return m_data.end(); }
        auto end()   const { return m_data.end(); }
        /// @}

        uint64_t typeVersion() const override { return m_typeVersion; }  ///< Per-type version counter.
        void bumpTypeVersion() { ++m_typeVersion; }  ///< Increment type version (called by ResourceManager::commit).

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
                m_refCounts[idx] = 0;
                return idx;
            }

            uint32_t idx = static_cast<uint32_t>(m_dataIndex.size());
            m_dataIndex.push_back(0);
            m_refCounts.push_back(0);
            m_generation.push_back({});
            return idx;
        }

    private:
        std::vector<GenerationIndex> m_generation;  ///< Sparse: alive + generation per slot
        std::vector<uint32_t> m_dataIndex;          ///< Sparse-to-dense mapping (free-list link when dead)
        std::vector<uint32_t> m_refCounts;          ///< Sparse: reference count per slot
        std::vector<uint32_t> m_dataId;             ///< Dense-to-sparse reverse mapping
        std::vector<T> m_data;                      ///< Dense: packed element storage

        uint32_t m_freeHead;                        ///< Head of intrusive free list (0 = empty)
        uint64_t m_typeVersion = 0;                 ///< Monotonic counter bumped on commit()
};

} // namespace Engine
