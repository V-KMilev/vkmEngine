#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "l_assert.h"
#include "core/memory/types.h"

namespace Vkm::Engine {

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

        /**
         * @brief Swap internal state with another allocator.
         *
         * Lets the containing Scene support a staging-then-swap load path without
         * breaking the no-copy/no-move invariant.
         */
        void swap(SlotAllocator& other) noexcept {
            using std::swap;
            swap(m_generation, other.m_generation);
            swap(m_freeList,   other.m_freeList);
            swap(m_liveCount,  other.m_liveCount);
        }

    public:
        /**
         * @brief Allocate a new handle with a unique index and current generation.
         * @return A StorageIndex handle with index > 0 and a valid generation.
         */
        StorageIndex allocate() {
            uint32_t idx = allocateSlot();
            m_generation[idx].setAlive(true);
            ++m_liveCount;
            return StorageIndex{idx, m_generation[idx].generation()};
        }

        /**
         * @brief Free a handle, bumping its generation and recycling the slot. @param id The handle to free.
         *
         * Must be alive (asserts). A handle that is not is refused rather than
         * acted on, because the damage is not confined to the caller: freeing a
         * slot twice underflows m_liveCount, and size() is what the prefab and
         * override walks bound their iteration by.
         */
        void free(StorageIndex id) {
            VKM_ASSERT(has(id), "SlotAllocator::free called with invalid handle");
            if (!has(id)) return;

            m_generation[id.index].setAlive(false);
            m_generation[id.index].bumpGeneration();
            m_freeList.push_back(id.index);
            --m_liveCount;
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
         * @brief Number of currently live (allocated and not freed) slots.
         */
        size_t size() const { return m_liveCount; }

        /**
         * @brief Rebuild the handle naming a sparse slot, generation included.
         *
         * Total, with the same bounds tolerance as isAliveAtIndex below: an
         * index past the allocator's reach yields the null handle instead of
         * reading out of bounds. A slot that is in reach but dead yields its
         * real generation, which is what makes the handle compare unequal to
         * the live one and fail has().
         *
         * @param index The sparse slot index.
         * @return The handle for that slot, null when the index is out of reach.
         */
        StorageIndex handleAt(uint32_t index) const {
            if (index >= m_generation.size()) return {};
            return StorageIndex{index, m_generation[index].generation()};
        }

        /**
         * @brief Check whether `index` currently holds a live slot, with bounds tolerance - returns false for indices past the allocator's reach.
         *
         * Slot 0 is reserved and always reports false.
         */
        bool isAliveAtIndex(uint32_t index) const {
            return index > 0
                && index < m_generation.size()
                && m_generation[index].alive();
        }

        /**
         * @brief Allocate a slot at a specific index.
         *
         * Used by SceneSerializer so loaded entities keep the slot indices they
         * had at save time (eliminates id-remap on Hierarchy::parent etc.).
         */
        StorageIndex allocateAt(uint32_t index) {
            VKM_ASSERT(index > 0, "SlotAllocator::allocateAt: slot 0 is reserved");

            // Growing to reach a sparse index (loading entities saved at, say,
            // 1,2,6,...) creates dead placeholder slots in the gap. Free-list
            // them so allocate() reuses the gap and size() counts only live
            // slots - otherwise every gap permanently leaks a slot and
            // entityCount() over-reports for the life of the scene.
            const uint32_t oldSize = static_cast<uint32_t>(m_generation.size());
            while (m_generation.size() <= index) m_generation.push_back({});
            for (uint32_t gap = oldSize; gap < index; ++gap) m_freeList.push_back(gap);

            VKM_ASSERT(!m_generation[index].alive(),
                "SlotAllocator::allocateAt: slot %u already alive", index);

            // The slot may still be sitting in the free list; it is left there
            // and skipped when it comes up, rather than searched for and erased.
            // Erasing meant a linear find plus a mid-vector shift per call, and
            // loading a scene whose indices have gaps calls this once per entity
            // with the gaps accumulating in the list - quadratic in entity count
            // for a scene that had ever had something deleted.
            m_generation[index].setAlive(true);
            ++m_liveCount;
            return StorageIndex{index, m_generation[index].generation()};
        }

        /**
         * @brief Invoke fn(index) for every currently-alive slot.
         *
         * Index 0 is reserved as the null/invalid slot and is never yielded. Order
         * is ascending by slot index (not allocation order).
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
         * O(slots) - used by Scene::clear for total reset, far cheaper than
         * iterating live entities and free()-ing each one when the goal is
         * to drop everything. Slot 0 stays reserved (untouched).
         */
        void clear() {
            m_freeList.clear();
            m_freeList.reserve(m_generation.size());
            for (uint32_t i = 1; i < m_generation.size(); ++i) {
                if (m_generation[i].alive()) {
                    m_generation[i].setAlive(false);
                    m_generation[i].bumpGeneration();
                }
                m_freeList.push_back(i);   // dead either way, so re-allocatable
            }
            m_liveCount = 0;
        }

    private:
        /**
         * @brief Obtain a free slot index, recycling first.
         *
         * @return The index of an allocatable slot.
         */
        uint32_t allocateSlot() {
            // Skip anything allocateAt claimed while it sat in the list. Each
            // stale entry is discarded once, so the scan stays amortised O(1).
            while (!m_freeList.empty()) {
                const uint32_t idx = m_freeList.back();
                m_freeList.pop_back();
                if (!m_generation[idx].alive()) return idx;
            }

            uint32_t idx = static_cast<uint32_t>(m_generation.size());
            m_generation.push_back({});
            return idx;
        }

    private:
        std::vector<GenerationIndex> m_generation; ///< Per-slot alive flag + generation counter
        std::vector<uint32_t> m_freeList;          ///< Recycled slot indices; may hold slots allocateAt has since claimed
        size_t m_liveCount = 0;                    ///< Live slots, tracked rather than derived from the free list
};

} // namespace Vkm::Engine
