#pragma once

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
* and a free list for O(1) slot recycling. Does not store any per-slot data — only
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
