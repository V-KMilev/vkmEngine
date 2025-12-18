#pragma once

#include <cstdint>
#include <vector>
#include <type_traits>
#include <stdexcept>

namespace Engine {

/**
 * @brief Generic storage for resources/assets, indexed by type-safe handles.
 * 
 * This class manages a contiguous array of resources (of type T)
 * and allows type-safe access and manipulation using a corresponding HandleType.
 * Handles are always 1-based; handle.value == 0 is invalid. 
 * Provides simple add, get, edit, and commit methods.
 * 
 * @tparam T           Type of the resource/asset to store (must have .version member for commit).
 * @tparam HandleType  Strongly typed handle to access resources (see resource_handle.h).
 */
template <typename T, typename HandleType>
class Storage {
    public:
        Storage() = default;
        ~Storage() = default;

        Storage(const Storage& other) = delete;
        Storage& operator=(const Storage& other) = delete;

        Storage(Storage && other) = delete;
        Storage& operator=(Storage && other) = delete;

    public:
        /**
         * @brief Add a new resource to storage.
         * @param resource The resource to add (rvalue-ref moved in).
         * @return HandleType A handle representing the index/location of the resource.
         */
        HandleType add(T && resource) {
            m_resources.push_back(std::move(resource));
            // Handles are 1-based: index 0 -> handle.value 1, etc.
            return HandleType{ uint32_t(m_resources.size()) };
        }

        /**
         * @brief Remove a resource from storage.
         * @param handle The handle referencing the resource.
         * @throws Assert if handle is invalid.
         */
        void remove(const HandleType& handle) {
            m_resources.erase(m_resources.begin() + idx(handle));
        }

        /**
         * @brief Get const access to a resource by handle.
         * @param handle The handle referencing the resource.
         * @return const T& Reference to the resource (read-only).
         * @throws Assert if handle is invalid.
         */
        const T& get(const HandleType& handle) const {
            return m_resources[idx(handle)];
        }

        /**
         * @brief Get mutable access to a resource for editing.
         * @param handle The handle referencing the resource.
         * @return T& Reference to the resource (mutable).
         * @note Triggers a debug assertion (VKM_ASSERT) if handle.value == 0 (invalid handle).
         */
        T& edit(const HandleType& handle) {
            return m_resources[idx(handle)];
        }

        /**
         * @brief Commit any changes made to a resource after editing.
         * Increments the resource's version number.
         * @param handle The handle referencing the resource.
         * @note Triggers a debug assertion (VKM_ASSERT) if handle.value == 0 (invalid handle).
         */
        void commit(const HandleType& handle) {
            ++m_resources[idx(handle)].version;
        }

        /**
         * @brief Returns the number of resources stored.
         * @return size_t Number of resources.
         */
        size_t size() const { return m_resources.size(); }

        /**
         * @brief Checks if storage is empty.
         * @return true if storage has no resources, false otherwise.
         */
        bool empty() const { return m_resources.empty(); }

        /**
         * @brief Get const reference to internal vector of all resources.
         * @return const std::vector<T>& All stored resources.
         */
        const std::vector<T>& getAll() const { return m_resources; }

    private:
        /**
         * @brief Convert a type-safe handle to an internal array index.
         * @param handle The typed handle (must be valid/nonzero).
         * @return size_t The 0-based index into m_resources vector.
         * @note Triggers a debug assertion (VKM_ASSERT) if handle.value == 0 (invalid handle).
         */
        [[nodiscard]]
        static size_t idx(const HandleType& handle) noexcept {
            // TODO: Think about how we can handle invalid handles.
            VKM_ASSERT(handle.value != 0, "Invalid handle");
            return size_t(handle.value - 1);
        }

    private:
        std::vector<T> m_resources;
};

} // namespace Engine
