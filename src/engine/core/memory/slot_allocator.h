#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "l_assert.h"
#include "core/memory/types.h"

namespace Vkm::Engine {

/**
 * @brief Lightweight slot allocator that issues generation-safe handles.
 *
 * Generation counters detect stale handles; a free list recycles in O(1). Stores
 * no per-slot data.
 */
class SlotAllocator {
    public:
        /**
         * @brief The highest index allocateAt will claim.
         *
         * allocateAt grows the table to an index often read from a file or a
         * datagram, so this bounds what a file or a peer can make it allocate.
         */
        static constexpr uint32_t MAX_CLAIMED_INDEX = 1u << 22;

    public:
        SlotAllocator() : m_generation({GenerationIndex{}}) {}
        ~SlotAllocator() = default;

        SlotAllocator(const SlotAllocator& other) = delete;
        SlotAllocator& operator=(const SlotAllocator& other) = delete;

        SlotAllocator(SlotAllocator && other) = delete;
        SlotAllocator& operator=(SlotAllocator && other) = delete;

    public:
        /**
         * @brief Allocate a new handle with a unique index and current generation.
         *
         * @param[out] recycled True when the slot came off the free list (used
         *        before, or an allocateAt gap); false when freshly grown, so
         *        nothing keyed by its index can be under it.
         * @return A handle with index > 0.
         */
        StorageIndex allocate(bool* recycled = nullptr) {
            bool reused = false;
            const uint32_t idx = allocateSlot(reused);
            if (recycled) *recycled = reused;
            m_generation[idx].setAlive(true);
            ++m_liveCount;
            return StorageIndex{idx, m_generation[idx].generation()};
        }

        /**
         * @brief Free a handle, bumping its generation and recycling the slot.
         *
         * A dead handle asserts and is refused: freeing twice underflows m_liveCount.
         *
         * @param id Live handle to free.
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
         * @brief Test whether a handle is still valid.
         * @param id Handle to validate.
         * @return True if alive and generation matches.
         */
        bool has(StorageIndex id) const {
            return id
                && id.index < m_generation.size()
                && m_generation[id.index].alive()
                && m_generation[id.index].generation() == id.generation;
        }

        /**
         * @brief Rebuild the handle naming a sparse slot, generation included.
         *
         * A dead slot yields its real generation, so the handle fails has().
         *
         * @param index Sparse slot index.
         * @return The handle for that slot, null when the index is out of reach.
         */
        StorageIndex handleAt(uint32_t index) const {
            if (index >= m_generation.size()) return {};
            return StorageIndex{index, m_generation[index].generation()};
        }

        /**
         * @brief Check whether @p index currently holds a live slot.
         *
         * @param index Sparse slot index; out of reach is fine.
         * @return True when that slot is live; never for slot 0.
         */
        bool isAliveAtIndex(uint32_t index) const {
            return index > 0
                && index < m_generation.size()
                && m_generation[index].alive();
        }

        /**
         * @brief Allocate a slot at a specific index.
         *
         * For a loader whose entities keep their saved slot indices. Gaps grown
         * across go onto the free list. A claimed slot still in the free list
         * is skipped when popped, not erased (a linear find per call).
         *
         * @param index Slot to claim; must be free.
         * @return The new handle, or null for slot 0, an index past
         *         MAX_CLAIMED_INDEX (test for these: the index often comes from
         *         outside the process), or a live slot (also asserts).
         */
        StorageIndex allocateAt(uint32_t index) {
            if (index == 0 || index > MAX_CLAIMED_INDEX) return {};

            // Highest first, so popping from the back hands the gap out lowest first.
            const uint32_t oldSize = static_cast<uint32_t>(m_generation.size());
            while (m_generation.size() <= index) m_generation.push_back({});
            for (uint32_t gap = index; gap-- > oldSize;) m_freeList.push_back(gap);

            VKM_ASSERT(
                !m_generation[index].alive(),
                "SlotAllocator::allocateAt: slot %u already alive",
                index
            );
            // Guarded too, so without asserts two owners never share a slot.
            if (m_generation[index].alive()) return {};

            m_generation[index].setAlive(true);
            ++m_liveCount;
            return StorageIndex{index, m_generation[index].generation()};
        }

        /**
         * @brief Invoke fn(index) for every currently-alive slot.
         *
         * @tparam Fn Callable taking a uint32_t slot index.
         * @param fn Called once per live slot, ascending by index; never slot 0.
         */
        template<typename Fn>
        void forEach(Fn&& fn) const {
            for (uint32_t i = 1; i < m_generation.size(); ++i) {
                if (m_generation[i].alive()) fn(i);
            }
        }

        /**
         * @brief Reset every slot to dead, bumping generations so outstanding handles go stale.
         */
        void clear() {
            m_freeList.clear();
            m_freeList.reserve(m_generation.size());
            // Highest first, so slot 1 comes out next, as from a new allocator.
            for (uint32_t i = static_cast<uint32_t>(m_generation.size()); i-- > 1;) {
                if (m_generation[i].alive()) {
                    m_generation[i].setAlive(false);
                    m_generation[i].bumpGeneration();
                }
                m_freeList.push_back(i);
            }
            m_liveCount = 0;
        }

        /**
         * @brief Swap internal state with another allocator.
         *
         * For Scene's staging-then-swap load, without copying or moving.
         *
         * @param other Allocator to trade state with.
         */
        void swap(SlotAllocator& other) noexcept {
            using std::swap;
            swap(m_generation, other.m_generation);
            swap(m_freeList,   other.m_freeList);
            swap(m_liveCount,  other.m_liveCount);
        }

        /**
         * @brief Number of live slots.
         *
         * @return The live slot count.
         */
        size_t size() const { return m_liveCount; }

        /**
         * @brief Number of slots the table spans, live or free, slot 0 included.
         *
         * @return One past the highest slot ever handed out.
         */
        size_t extent() const { return m_generation.size(); }

    private:
        /**
         * @brief Obtain a free slot index, recycling first.
         *
         * @param[out] reused Whether the index came off the free list.
         * @return The index of an allocatable slot.
         */
        uint32_t allocateSlot(bool& reused) {
            // Skip slots allocateAt claimed; each is discarded once, so amortised O(1).
            while (!m_freeList.empty()) {
                const uint32_t idx = m_freeList.back();
                m_freeList.pop_back();
                if (!m_generation[idx].alive()) {
                    reused = true;
                    return idx;
                }
            }

            reused = false;
            const uint32_t idx = static_cast<uint32_t>(m_generation.size());
            m_generation.push_back({});
            return idx;
        }

    private:
        std::vector<GenerationIndex> m_generation;
        std::vector<uint32_t> m_freeList;   ///< May hold slots allocateAt has since claimed.
        size_t m_liveCount = 0;
};

} // namespace Vkm::Engine
