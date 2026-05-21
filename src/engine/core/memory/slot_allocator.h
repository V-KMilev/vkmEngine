#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "l_assert.h"
#include "core/memory/types.h"

namespace Engine {

/**
* @class SlotAllocator
* @brief Lightweight slot allocator that issues generation-safe handles.
*
* Manages a pool of indices with generation counters for stale-handle detection
* and a free list for O(1) slot recycling. Does not store any per-slot data - only
* the metadata needed for allocation, deallocation, and validation.
*/
class SlotAllocator {
    public:
        SlotAllocator() : m_generation({GenerationIndex{}}) {}
        ~SlotAllocator() = default;

        SlotAllocator(const SlotAllocator& other) = delete;
        SlotAllocator& operator=(const SlotAllocator& other) = delete;

        SlotAllocator(SlotAllocator && other) = delete;
        SlotAllocator& operator=(SlotAllocator && other) = delete;

        /// @brief Swap internal state with another allocator. Lets the
        /// containing Scene support a staging-then-swap load path without
        /// breaking the no-copy/no-move invariant.
        void swap(SlotAllocator& other) noexcept {
            using std::swap;
            swap(m_generation, other.m_generation);
            swap(m_freeList,   other.m_freeList);
        }

    public:
        /**
        * @brief Allocate a new handle with a unique index and current generation.
        * @return A StorageIndex handle with index > 0 and a valid generation.
        */
        StorageIndex allocate() {
            uint32_t idx = allocateSlot();
            m_generation[idx].setAlive(true);
            return StorageIndex{idx, m_generation[idx].generation()};
        }

        /**
        * @brief Free a handle, bumping its generation and recycling the slot.
        * @param id The handle to free. Must be alive (asserts).
        */
        void free(StorageIndex id) {
            VKM_ASSERT(has(id), "SlotAllocator::free called with invalid handle");
            m_generation[id.index].setAlive(false);
            m_generation[id.index].bumpGeneration();
            m_freeList.push_back(id.index);
        }

        /**
        * @brief Test whether a handle is still valid (alive with matching generation).
        * @param id The handle to validate.
        * @return True if alive and generation matches.
        */
        bool has(StorageIndex id) const {
            return id
                && id.index < m_generation.size()
                && m_generation[id.index].alive()
                && m_generation[id.index].generation() == id.generation;
        }

        /**
        * @brief Get the current generation for a sparse slot index.
        * @param index The sparse slot index. Must be in bounds.
        * @return The current generation counter for that slot.
        */
        /**
        * @brief Number of currently live (allocated and not freed) slots.
        */
        size_t size() const {
            return m_generation.size() - 1 - m_freeList.size();
        }

        uint32_t generationOf(uint32_t index) const {
            VKM_ASSERT(index < m_generation.size(), "SlotAllocator::generationOf out of bounds");
            return m_generation[index].generation();
        }

        /**
         * @brief Check whether `index` currently holds a live slot, with
         * bounds tolerance — returns false for indices past the allocator's
         * reach. Slot 0 is reserved and always reports false.
         */
        bool isAliveAtIndex(uint32_t index) const {
            return index > 0
                && index < m_generation.size()
                && m_generation[index].alive();
        }

        /**
         * @brief Allocate a slot at a specific index. Used by SceneSerializer
         * so loaded entities keep the slot indices they had at save time
         * (eliminates id-remap on Hierarchy::parent etc.).
         *
         * - Grows the underlying array with dead placeholders up to `index`.
         * - Removes `index` from the free list if present.
         * - Asserts in debug if the slot is already alive (collision).
         */
        StorageIndex allocateAt(uint32_t index) {
            VKM_ASSERT(index > 0, "SlotAllocator::allocateAt: slot 0 is reserved");
            while (m_generation.size() <= index) m_generation.push_back({});

            VKM_ASSERT(!m_generation[index].alive(),
                "SlotAllocator::allocateAt: slot %u already alive", index);

            auto it = std::find(m_freeList.begin(), m_freeList.end(), index);
            if (it != m_freeList.end()) m_freeList.erase(it);

            m_generation[index].setAlive(true);
            return StorageIndex{index, m_generation[index].generation()};
        }

        /**
         * @brief Invoke fn(index) for every currently-alive slot. Index 0 is
         * reserved as the null/invalid slot and is never yielded. Order is
         * ascending by slot index (not allocation order).
         */
        template<typename Fn>
        void forEach(Fn&& fn) const {
            for (uint32_t i = 1; i < m_generation.size(); ++i) {
                if (m_generation[i].alive()) fn(i);
            }
        }

        /**
         * @brief Reset every slot to dead in one pass, bumping generations
         * so any outstanding handles correctly compare as stale.
         *
         * O(slots) — used by Scene::clear for total reset, far cheaper than
         * iterating live entities and free()-ing each one when the goal is
         * to drop everything. Slot 0 stays reserved (untouched).
         */
        void clear() {
            for (uint32_t i = 1; i < m_generation.size(); ++i) {
                if (m_generation[i].alive()) {
                    m_generation[i].setAlive(false);
                    m_generation[i].bumpGeneration();
                }
            }
            m_freeList.clear();
            // Every slot above index 0 is now dead and re-allocatable.
            for (uint32_t i = 1; i < m_generation.size(); ++i) {
                m_freeList.push_back(i);
            }
        }

    private:
        /// @brief Pop a slot from the free list, or grow the generation array if empty.
        uint32_t allocateSlot() {
            if (!m_freeList.empty()) {
                uint32_t idx = m_freeList.back();
                m_freeList.pop_back();
                return idx;
            }

            uint32_t idx = static_cast<uint32_t>(m_generation.size());
            m_generation.push_back({});
            return idx;
        }

    private:
        std::vector<GenerationIndex> m_generation; ///< Per-slot alive flag + generation counter
        std::vector<uint32_t> m_freeList;          ///< Recycled slot indices
};

} // namespace Engine
