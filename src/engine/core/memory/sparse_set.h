#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <type_traits>

#include "l_assert.h"

namespace Engine {

/**
 * @class ISparseSet
 * @brief Type-erased interface for SparseSet, enabling heterogeneous storage in registries.
 */
class ISparseSet {
    public:
        virtual ~ISparseSet() = default;
        virtual void remove(uint32_t key) = 0;
        virtual bool has(uint32_t key) const = 0;
        virtual size_t size() const = 0;
        virtual void compact() = 0;
        /**
         * @brief Drop every element. Used by Scene::clear and shutdown
         *        paths that want O(types) tear-down rather than
         *        O(entities x types) one-element-at-a-time removal.
         */
        virtual void clear() = 0;
};

/**
 * @class SparseSet
 * @brief Dense-packed storage indexed by external uint32_t keys.
 *
 * Maps uint32_t keys to densely packed elements for cache-friendly O(n)
 * iteration. Provides O(1) add, remove, has, and get.
 *
 * SparseSet does not manage slot allocation or generation counters - the
 * caller owns the key lifecycle (Scene pairs this with SlotAllocator for
 * entities; ResourceManager pairs it with a per-type allocator for assets).
 *
 * Removal is swap-and-pop with a memcpy fast path for trivially copyable
 * types.
 *
 * @tparam T Element type to store.
 */
template<typename T>
class SparseSet : public ISparseSet {
    public:
        SparseSet() = default;
        ~SparseSet() override = default;

        SparseSet(const SparseSet& other) = delete;
        SparseSet& operator=(const SparseSet& other) = delete;

        SparseSet(SparseSet && other) = delete;
        SparseSet& operator=(SparseSet && other) = delete;

    public:
        /**
         * @brief Insert an element at the given key.
         * @param key External sparse key. Must not be 0 or already present.
         * @param value Element to insert.
         * @return Reference to the stored element.
         */
        T& add(uint32_t key, T && value)     { return addInternal(key, std::move(value)); }
        T& add(uint32_t key, const T& value) { return addInternal(key, value); }

        /**
         * @brief Remove the element at the given key via swap-and-pop.
         * @param key External sparse key. Must be present (asserts).
         */
        void remove(uint32_t key) override {
            VKM_ASSERT(has(key), "SparseSet::remove called with invalid key");

            uint32_t dataIdx = m_dataIndex[key];
            uint32_t lastIdx = static_cast<uint32_t>(m_data.size() - 1);

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
            m_dataIndex[key] = EMPTY;
        }

        /**
         * @brief Test whether a key is present (non-virtual, for hot paths).
         * @param key External sparse key.
         * @return True if the key maps to a live element.
         */
        bool contains(uint32_t key) const {
            return key < m_dataIndex.size() && m_dataIndex[key] != EMPTY;
        }

        /** @brief Virtual override forwarding to contains() for type-erased callers. */
        bool has(uint32_t key) const override { return contains(key); }

        /**
         * @brief Access the element at the given key.
         * @param key External sparse key. Must be present (asserts).
         * @return Reference to the stored element.
         */
        T&       get(uint32_t key)       { VKM_ASSERT(has(key), "SparseSet::get called with invalid key"); return m_data[m_dataIndex[key]]; }
        const T& get(uint32_t key) const { VKM_ASSERT(has(key), "SparseSet::get called with invalid key"); return m_data[m_dataIndex[key]]; }

    public:
        /**
         * @brief Iterate all live elements densely (no holes).
         *
         * Calls fn(uint32_t key, T&) for each element in packed order.
         *
         * @param fn Callable with signature void(uint32_t, T&).
         */
        template<typename Fn>
        void forEach(Fn&& fn) {
            for (uint32_t i = 0; i < m_data.size(); ++i) {
                fn(m_dataId[i], m_data[i]);
            }
        }

        template<typename Fn>
        void forEach(Fn&& fn) const {
            for (uint32_t i = 0; i < m_data.size(); ++i) {
                fn(m_dataId[i], m_data[i]);
            }
        }

        size_t size() const override { return m_data.size(); }  ///< Number of live elements.

        /**
         * @brief Drop every element. The dense and sparse arrays empty;
         *        their capacity is retained so a subsequent rebuild
         *        doesn't re-pay allocation cost.
         */
        void clear() override {
            m_data.clear();
            m_dataId.clear();
            // Mark every sparse slot empty without reallocating.
            std::fill(m_dataIndex.begin(), m_dataIndex.end(), EMPTY);
        }

        /** @brief Shrink the sparse array to fit only live keys, reclaiming wasted memory. */
        void compact() override {
            if (m_data.empty()) {
                m_dataIndex.clear();
                m_dataIndex.shrink_to_fit();
                return;
            }
            uint32_t maxKey = 0;
            for (uint32_t i = 0; i < m_dataId.size(); ++i) {
                if (m_dataId[i] > maxKey) maxKey = m_dataId[i];
            }
            if (maxKey + 1 < m_dataIndex.size()) {
                m_dataIndex.resize(maxKey + 1);
                m_dataIndex.shrink_to_fit();
            }
        }

        T*       data()       { return m_data.data(); } ///< Raw pointer to the packed dense array.
        const T* data() const { return m_data.data(); } ///< @copydoc data()

        /** @brief Access the sparse key at a dense index (for index-based parallel iteration). */
        uint32_t keyAt(uint32_t denseIndex) const { return m_dataId[denseIndex]; }
        /** @brief Access the element at a dense index (for index-based parallel iteration). */
        T&       dataAt(uint32_t denseIndex)       { return m_data[denseIndex]; }
        const T& dataAt(uint32_t denseIndex) const { return m_data[denseIndex]; }

    private:
        static constexpr uint32_t EMPTY = UINT32_MAX;

        /** @brief Grows the sparse array to fit @p key if needed. */
        void ensureCapacity(uint32_t key) {
            if (key >= m_dataIndex.size())
                m_dataIndex.resize(key + 1, EMPTY);
        }

        /** @brief Validates the key, emplaces into the dense array, and wires up both mappings. */
        template<typename... Args>
        T& addInternal(uint32_t key, Args&&... args) {
            VKM_ASSERT(key != EMPTY, "SparseSet::add key cannot be EMPTY sentinel");
            VKM_ASSERT(key != 0, "SparseSet::add key 0 is reserved");
            ensureCapacity(key);
            VKM_ASSERT(!has(key), "SparseSet::add key already present");

            uint32_t dataIdx = static_cast<uint32_t>(m_data.size());

            m_data.emplace_back(std::forward<Args>(args)...);
            m_dataId.push_back(key);
            m_dataIndex[key] = dataIdx;

            return m_data[dataIdx];
        }

    private:
        std::vector<uint32_t> m_dataIndex; ///< Sparse-to-dense mapping (EMPTY = absent)
        std::vector<uint32_t> m_dataId;    ///< Dense-to-sparse reverse mapping
        std::vector<T>        m_data;      ///< Dense: packed element storage
};

} // namespace Engine
