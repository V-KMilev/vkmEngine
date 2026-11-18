#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "vkm_log/assert.h"
#include "base/container/storage_index.h"

namespace Vkm::Engine {

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
 * @brief Lightweight slot allocator that issues generation-safe handles.
 *
 * Generation counters detect stale handles; a free list recycles in O(1). Stores
 * no per-slot data.
 */
class SlotAllocator {
    public:
        static constexpr uint32_t MAX_CLAIMED_INDEX = 1u << 22;  ///< The highest index allocateAt will claim.
                                                                 ///< allocateAt grows the table to an index
                                                                 ///< often read from a file or a datagram, so
                                                                 ///< this bounds what a file or a peer can
                                                                 ///< make it allocate.

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
         * is skipped when popped, not erased.
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
         *
         * The floor stays (keepBelow): it is the caller's, not the slots'.
         */
        void clear() {
            m_freeList.clear();
            m_kept.clear();
            m_freeList.reserve(m_generation.size());
            // Highest first, so the lowest slot allocate() may hand out comes out next.
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
         * @brief Every slot's generation, slot 0's included: a live slot's id's, a free slot's next.
         *
         * @return What startAt() takes to number another allocator's slots as these are.
         */
        std::vector<uint32_t> generations() const {
            std::vector<uint32_t> each;
            each.reserve(m_generation.size());
            for (const GenerationIndex slot : m_generation) each.push_back(slot.generation());
            return each;
        }

        /**
         * @brief Number every slot as @p generations does, all of them free.
         *
         * For a world rebuilt under the ids it had: allocateAt then claims each slot at the generation
         * it had, and a slot that was free hands out no id it gave before. Asserts and refuses while a
         * slot is live. The floor stays, as clear() leaves it.
         *
         * @param generations What generations() answered; slot 0's is not read.
         */
        void startAt(const std::vector<uint32_t>& generations) {
            VKM_ASSERT(m_liveCount == 0, "SlotAllocator::startAt called with live slots");
            if (m_liveCount != 0) return;

            m_generation.assign(std::max<size_t>(generations.size(), 1), GenerationIndex{});
            m_freeList.clear();
            m_kept.clear();
            // Highest first, so the lowest slot allocate() may hand out comes out next.
            for (uint32_t i = static_cast<uint32_t>(m_generation.size()); i-- > 1;) {
                m_generation[i].value = generations[i] & GenerationIndex::GEN_MASK;
                m_freeList.push_back(i);
            }
        }

        /**
         * @brief Hand out no slot below @p floor from allocate(): those are left to allocateAt.
         *
         * A client's own entities go above where the server's are named, so each of the server's
         * finds its own slot free (NetSession). Zero hands out every slot again.
         *
         * @param floor The lowest slot allocate() returns.
         */
        void keepBelow(uint32_t floor) {
            m_floor = floor;
            if (floor > 0) return;
            m_freeList.insert(m_freeList.end(), m_kept.begin(), m_kept.end());
            m_kept.clear();
        }

        /// The lowest slot allocate() returns; 0 for none kept.
        uint32_t floor() const { return m_floor; }

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
            swap(m_kept,       other.m_kept);
            swap(m_liveCount,  other.m_liveCount);
            swap(m_floor,      other.m_floor);
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
            // Skip slots allocateAt claimed; each is discarded once, so amortized O(1).
            while (!m_freeList.empty()) {
                const uint32_t idx = m_freeList.back();
                m_freeList.pop_back();
                if (m_generation[idx].alive()) continue;
                // Set aside, not dropped: handed out again once the floor goes.
                if (idx < m_floor) {
                    m_kept.push_back(idx);
                    continue;
                }
                reused = true;
                return idx;
            }

            // Grown past the floor, what lies below it is free for allocateAt.
            while (m_generation.size() < m_floor) {
                m_kept.push_back(static_cast<uint32_t>(m_generation.size()));
                m_generation.push_back({});
            }
            reused = false;
            const uint32_t idx = static_cast<uint32_t>(m_generation.size());
            m_generation.push_back({});
            return idx;
        }

    private:
        std::vector<GenerationIndex> m_generation;
        std::vector<uint32_t>        m_freeList;       ///< May hold slots allocateAt has since claimed.
        std::vector<uint32_t>        m_kept;           ///< Free slots below the floor, for when it goes; as
                                                       ///< m_freeList, some allocateAt may have claimed
                                                       ///< since.
        size_t                       m_liveCount = 0;
        uint32_t                     m_floor     = 0;  ///< See keepBelow().
};

} // namespace Vkm::Engine
