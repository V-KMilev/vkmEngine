#pragma once

#include <algorithm>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "l_assert.h"
#include "logger.h"

#include "core/memory/storage.h"
#include "core/memory/types.h"
#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Engine {

/**
 * @brief Open type-erased resource registry with typed handles and per-type version tracking.
 *
 * Any type inheriting from Resource can be stored without modifying this class.
 * Internally dispatches operations to type-specific Storage<T> instances via typeId<T>().
 *
 * This class is non-copyable and non-movable.
 */
class ResourceManager {
    public:
        ResourceManager() = default;
        ~ResourceManager() = default;

        ResourceManager(const ResourceManager& other) = delete;
        ResourceManager& operator=(const ResourceManager& other) = delete;

        ResourceManager(ResourceManager && other) = delete;
        ResourceManager& operator=(ResourceManager && other) = delete;

    public:
        /**
         * @brief Add a new resource to the manager.
         *
         * @tparam ResourceType The resource type (must inherit from Resource).
         * @param resource The resource instance to add (will be moved).
         * @return The handle for the newly added resource.
         */
        template<typename ResourceType>
        auto add(ResourceType && resource) {
            using T = std::remove_cv_t<std::remove_reference_t<ResourceType>>;

            StorageIndex key = getStorage<T>().add(std::forward<ResourceType>(resource));
            return Handle<T>{key};
        }

        /**
         * @brief Remove a resource by handle.
         *
         * @tparam HandleType The handle type associated with the resource.
         * @param handle Handle referencing the resource to remove.
         */
         template<typename HandleType>
         void remove(const HandleType& handle) {
             using T = typename HandleType::resource_t;
             if (hasDependents(handle)) {
                 LOG_WARNING("Removing resource %u with %zu active dependents",
                     handle.id(), dependentCount(handle));
             }
             // Clean up any dependency entries where this resource is depended on
             uint64_t key = packKey<T>(handle.key.index);
             m_dependents.erase(key);
             getStorage<T>().remove(handle.key);
         }

        /**
         * @brief Get const access to a resource by handle.
         *
         * @tparam HandleType The handle type associated with the resource.
         * @param handle Handle used to look up the resource.
         * @return const reference to the resource.
         */
        template<typename HandleType>
        const auto& get(const HandleType& handle) const {
            using T = typename HandleType::resource_t;
            return getStorage<T>().get(handle.key);
        }

        /**
         * @brief Get mutable access to a resource for editing by handle.
         *
         * @tparam HandleType The handle type associated with the resource.
         * @param handle Handle used to look up the resource.
         * @return mutable reference to the resource.
         */
        template<typename HandleType>
        auto& edit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            return getStorage<T>().get(handle.key);
        }

        /**
         * @brief Commit changes to a resource (increments both resource and type version).
         *
         * @tparam HandleType The handle type associated with the resource.
         * @param handle Handle referencing the resource to commit.
         */
        template<typename HandleType>
        void commit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            static_assert(std::is_base_of_v<Resource, T>, "Resource type must inherit from Resource to use commit().");
            auto& storage = getStorage<T>();
            ++storage.get(handle.key).version;
            storage.bumpTypeVersion();
            ++m_globalVersion;
        }

        /**
         * @brief Increment the reference count for a resource handle.
         */
        template<typename HandleType>
        void acquire(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            getStorage<T>().acquire(handle.key);
        }

        /**
         * @brief Decrement the reference count for a resource handle.
         */
        template<typename HandleType>
        void release(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            getStorage<T>().release(handle.key);
        }

        /**
         * @brief Query the reference count for a resource handle.
         */
        template<typename HandleType>
        uint32_t refCount(const HandleType& handle) const {
            using T = typename HandleType::resource_t;
            return getStorage<T>().refCount(handle.key);
        }

        /**
         * @brief Global version counter, incremented on every commit(). Used for sync skip.
         */
        uint64_t getGlobalVersion() const { return m_globalVersion; }

        /**
         * @brief Per-type version counter. Only incremented when resources of type T are committed.
         */
        template<typename T>
        uint64_t getTypeVersion() const {
            TypeId id = typeId<T>();
            if (id >= m_storages.size() || !m_storages[id]) return 0;
            return m_storages[id]->typeVersion();
        }

        // --- Resource dependency tracking ---

        /**
         * @brief Register that @p from depends on @p to (e.g., material depends on texture).
         */
        template<typename FromHandle, typename ToHandle>
        void addDependency(const FromHandle& from, const ToHandle& to) {
            uint64_t fromKey = packKey<typename FromHandle::resource_t>(from.key.index);
            uint64_t toKey   = packKey<typename ToHandle::resource_t>(to.key.index);
            m_dependents[toKey].push_back(fromKey);
        }

        /**
         * @brief Unregister a dependency from @p from to @p to.
         */
        template<typename FromHandle, typename ToHandle>
        void removeDependency(const FromHandle& from, const ToHandle& to) {
            uint64_t fromKey = packKey<typename FromHandle::resource_t>(from.key.index);
            uint64_t toKey   = packKey<typename ToHandle::resource_t>(to.key.index);
            auto it = m_dependents.find(toKey);
            if (it != m_dependents.end()) {
                auto& vec = it->second;
                vec.erase(std::remove(vec.begin(), vec.end(), fromKey), vec.end());
                if (vec.empty()) m_dependents.erase(it);
            }
        }

        /**
         * @brief Check if any resources depend on the given handle.
         */
        template<typename HandleType>
        bool hasDependents(const HandleType& handle) const {
            uint64_t key = packKey<typename HandleType::resource_t>(handle.key.index);
            auto it = m_dependents.find(key);
            return it != m_dependents.end() && !it->second.empty();
        }

        /**
         * @brief Get the number of resources that depend on the given handle.
         */
        template<typename HandleType>
        size_t dependentCount(const HandleType& handle) const {
            uint64_t key = packKey<typename HandleType::resource_t>(handle.key.index);
            auto it = m_dependents.find(key);
            return it != m_dependents.end() ? it->second.size() : 0;
        }

    private:
        /**
         * @brief Get or create the typed storage for resource type T.
         */
        template<typename T>
        Storage<T>& getStorage() {
            TypeId id = typeId<T>();
            if (id >= m_storages.size()) {
                m_storages.resize(id + 1);
            }
            if (!m_storages[id]) {
                m_storages[id] = std::make_unique<Storage<T>>();
            }
            return static_cast<Storage<T>&>(*m_storages[id]);
        }

        /**
         * @brief Get const access to the typed storage for resource type T.
         */
        template<typename T>
        const Storage<T>& getStorage() const {
            TypeId id = typeId<T>();
            VKM_ASSERT(id < m_storages.size() && m_storages[id], "Storage not registered for this type");
            return static_cast<const Storage<T>&>(*m_storages[id]);
        }

        /**
         * @brief Pack typeId + handle index into a single 64-bit key for the dependency graph.
         */
        template<typename T>
        static uint64_t packKey(uint32_t index) {
            return (static_cast<uint64_t>(typeId<T>()) << 32) | index;
        }

    private:
        std::vector<std::unique_ptr<IStorage>> m_storages;
        uint64_t m_globalVersion = 0;

        /// Dependency graph: key = resource being depended on, value = list of resources that depend on it
        std::unordered_map<uint64_t, std::vector<uint64_t>> m_dependents;
};

} // namespace Engine
