#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <type_traits>

#include "l_assert.h"

namespace Vkm::Engine {

/**
 * @brief Type-erased interface for SparseSet, so one registry holds every type.
 */
class ISparseSet {
    public:
        ISparseSet() = default;
        virtual ~ISparseSet() = default;

        ISparseSet(const ISparseSet& other) = delete;
        ISparseSet& operator=(const ISparseSet& other) = delete;

        ISparseSet(ISparseSet && other) = delete;
        ISparseSet& operator=(ISparseSet && other) = delete;

        /**
         * @brief Remove the element at the given key, if this set holds one.
         *
         * @param key External sparse key.
         */
        virtual void removeIfPresent(uint32_t key) = 0;

        /**
         * @brief Release the sparse array's slack, keeping every live element.
         *
         * For after a bulk build (see Scene::compact).
         */
        virtual void compact() = 0;
};

/**
 * @brief Dense-packed storage indexed by external uint32_t keys.
 *
 * Dense iteration; O(1) add, remove, contains and get; swap-and-pop removal.
 * The caller owns the key lifecycle and generations.
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
         * @param key External sparse key; not 0, not already present.
         * @param value Element to insert.
         * @return The stored element.
         */
        T& add(uint32_t key, T && value)     { return addInternal(key, std::move(value)); }
        T& add(uint32_t key, const T& value) { return addInternal(key, value); }

        /**
         * @brief Remove the element at the given key via swap-and-pop.
         * @param key External sparse key; must be present.
         */
        void remove(uint32_t key) {
            VKM_ASSERT(contains(key), "SparseSet::remove called with invalid key");
            // Guarded too: a build without asserts would index m_data with garbage.
            if (!contains(key)) return;

            uint32_t dataIdx = m_dataIndex[key];
            uint32_t lastIdx = static_cast<uint32_t>(m_data.size() - 1);

            if (dataIdx != lastIdx) {
                m_data[dataIdx] = std::move(m_data[lastIdx]);

                m_dataId[dataIdx]              = m_dataId[lastIdx];
                m_dataIndex[m_dataId[dataIdx]] = dataIdx;
            }

            m_data.pop_back();
            m_dataId.pop_back();
            m_dataIndex[key] = EMPTY;
        }

        /**
         * @brief Remove the element at the given key if one is present.
         *
         * @param key External sparse key; absent is a no-op.
         */
        void removeIfPresent(uint32_t key) override {
            if (contains(key)) remove(key);
        }

        /**
         * @brief Test whether a key is present.
         * @param key External sparse key.
         * @return True if the key maps to a live element.
         */
        bool contains(uint32_t key) const {
            return key < m_dataIndex.size() && m_dataIndex[key] != EMPTY;
        }

        /**
         * @brief Access the element at the given key.
         * @param key External sparse key; must be present.
         * @return The stored element.
         */
        T& get(uint32_t key) {
            VKM_ASSERT(contains(key), "SparseSet::get called with invalid key");
            return m_data[m_dataIndex[key]];
        }

        const T& get(uint32_t key) const {
            VKM_ASSERT(contains(key), "SparseSet::get called with invalid key");
            return m_data[m_dataIndex[key]];
        }

    public:
        /**
         * @brief Iterate all live elements densely (no holes).
         *
         * Do not add or remove while this runs: swap-and-pop skips the moved
         * element, and an add can reallocate under @p fn. Collect keys and act
         * afterwards.
         *
         * @tparam Fn Callable as void(uint32_t, T&).
         * @param fn Called once per live element, with its key, in packed order.
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

        /**
         * @brief Number of live elements.
         *
         * @return The dense array's length.
         */
        size_t size() const { return m_data.size(); }

        /**
         * @brief Drop every element.
         *
         * Capacity is kept, so a rebuild does not allocate again.
         */
        void clear() {
            m_data.clear();
            m_dataId.clear();
            std::fill(m_dataIndex.begin(), m_dataIndex.end(), EMPTY);
        }

        /**
         * @brief Shrink the sparse array to fit only live keys, reclaiming wasted memory.
         *
         * Always shrinks, since a bulk build in ascending key order leaves mostly
         * geometric over-allocation rather than entries past the highest key. The
         * dense arrays are unaffected.
         */
        void compact() override {
            if (m_data.empty()) {
                m_dataIndex.clear();
            } else {
                uint32_t maxKey = 0;
                for (uint32_t i = 0; i < m_dataId.size(); ++i) {
                    if (m_dataId[i] > maxKey) maxKey = m_dataId[i];
                }
                if (maxKey + 1 < m_dataIndex.size()) m_dataIndex.resize(maxKey + 1);
            }
            m_dataIndex.shrink_to_fit();
        }

        /**
         * @brief Access the sparse key stored at a dense index.
         *
         * With size() and dataAt(), for index-based parallel iteration.
         *
         * @param denseIndex Position in packed order (< size()).
         * @return The key at that slot.
         */
        uint32_t keyAt(uint32_t denseIndex) const { return m_dataId[denseIndex]; }

        /**
         * @brief Access the element stored at a dense index.
         *
         * @param denseIndex Position in packed order (< size()).
         * @return The element at that slot.
         */
        T&       dataAt(uint32_t denseIndex)       { return m_data[denseIndex]; }
        const T& dataAt(uint32_t denseIndex) const { return m_data[denseIndex]; }

    private:
        static constexpr uint32_t EMPTY = UINT32_MAX;

        /**
         * @brief Grow the sparse array so it can index @p key.
         *
         * @param key Key that must become addressable.
         */
        void ensureCapacity(uint32_t key) {
            if (key >= m_dataIndex.size())
                m_dataIndex.resize(key + 1, EMPTY);
        }

        /**
         * @brief Validate the key, emplace into the dense array, and wire up both mappings.
         *
         * @tparam Args Constructor argument types.
         * @param key Key for the new element.
         * @param args Forwarded to T's constructor.
         * @return The new element.
         */
        template<typename... Args>
        T& addInternal(uint32_t key, Args&&... args) {
            VKM_ASSERT(key != EMPTY, "SparseSet::add key cannot be EMPTY sentinel");
            VKM_ASSERT(key != 0, "SparseSet::add key 0 is reserved");
            ensureCapacity(key);
            VKM_ASSERT(!contains(key), "SparseSet::add key already present");
            // Guarded too: without asserts the key would strand its first entry.
            if (contains(key)) return m_data[m_dataIndex[key]];

            uint32_t dataIdx = static_cast<uint32_t>(m_data.size());

            m_data.emplace_back(std::forward<Args>(args)...);
            m_dataId.push_back(key);
            m_dataIndex[key] = dataIdx;

            return m_data[dataIdx];
        }

    private:
        std::vector<uint32_t> m_dataIndex; ///< Sparse to dense; EMPTY = absent.
        std::vector<uint32_t> m_dataId;    ///< Dense to sparse.
        std::vector<T>        m_data;
};

} // namespace Vkm::Engine
