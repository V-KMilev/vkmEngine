#pragma once

#include <algorithm>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "l_assert.h"
#include "logger.h"

#include "core/memory/slot_allocator.h"
#include "core/memory/sparse_set.h"
#include "core/memory/types.h"
#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Engine {

/**
 * @brief Open type-erased resource registry with typed handles and per-type version tracking.
 *
 * Any type inheriting from Resource can be stored without modifying this class.
 * Mirrors Scene's design: per type we keep a SlotAllocator (handle lifetime),
 * a SparseSet<T> (storage), a parallel refcount vector, and a typeVersion counter.
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
            auto& slot = getSlot<T>();

            StorageIndex key = slot.allocator->allocate();
            storageOf<T>(slot).add(key.index, std::forward<ResourceType>(resource));

            if (key.index >= slot.refCounts.size()) {
                slot.refCounts.resize(key.index + 1, 0);
            }
            slot.refCounts[key.index] = 0;

            return Handle<T>{key};
        }

        /// @brief Convenience overload: stamp a name onto the asset before insertion.
        template<typename ResourceType>
        auto add(ResourceType && resource, std::string name) {
            resource.name = std::move(name);
            return add(std::forward<ResourceType>(resource));
        }

        /**
         * @brief Remove a resource by handle.
         */
        template<typename HandleType>
        void remove(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            if (hasDependents(handle)) {
                LOG_WARNING("Removing resource %u with %zu active dependents",
                    handle.id(), dependentCount(handle));
            }
            uint64_t depKey = packKey<T>(handle.key.index);
            m_dependents.erase(depKey);

            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::remove invalid handle");
            VKM_ASSERT(slot.refCounts[handle.key.index] == 0,
                "ResourceManager::remove called on resource with non-zero refCount (%u)",
                slot.refCounts[handle.key.index]);
            storageOf<T>(slot).remove(handle.key.index);
            slot.allocator->free(handle.key);
        }

        /// @brief Get const access to a resource by handle.
        template<typename HandleType>
        const auto& get(const HandleType& handle) const {
            using T = typename HandleType::resource_t;
            const auto& slot = getSlotConst<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::get invalid handle");
            return storageOfConst<T>(slot).get(handle.key.index);
        }

        /// @brief Get mutable access to a resource for editing by handle.
        template<typename HandleType>
        auto& edit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::edit invalid handle");
            return storageOf<T>(slot).get(handle.key.index);
        }

        /**
         * @brief Commit changes to a resource (bumps resource and per-type versions).
         */
        template<typename HandleType>
        void commit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            static_assert(std::is_base_of_v<Resource, T>, "Resource type must inherit from Resource to use commit().");
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::commit invalid handle");
            ++storageOf<T>(slot).get(handle.key.index).version;
            ++slot.typeVersion;
            ++m_globalVersion;
        }

        /// @brief Increment the reference count for a resource handle.
        template<typename HandleType>
        void acquire(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::acquire invalid handle");
            ++slot.refCounts[handle.key.index];
        }

        /// @brief Decrement the reference count for a resource handle.
        template<typename HandleType>
        void release(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::release invalid handle");
            VKM_ASSERT(slot.refCounts[handle.key.index] > 0, "ResourceManager::release with zero refCount");
            --slot.refCounts[handle.key.index];
        }

        /// @brief Query the reference count for a resource handle.
        template<typename HandleType>
        uint32_t refCount(const HandleType& handle) const {
            using T = typename HandleType::resource_t;
            const auto& slot = getSlotConst<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::refCount invalid handle");
            return slot.refCounts[handle.key.index];
        }

        /**
         * @brief Find a resource by its `name` field. Returns a default
         * (invalid) handle if the type is unregistered or no asset matches.
         * Linear scan — intended for editor / scene-load lookups, not hot paths.
         */
        template<typename T>
        Handle<T> findByName(const std::string& name) const {
            TypeId id = typeId<T>();
            if (id >= m_slots.size() || !m_slots[id]) return {};
            const auto& slot = *m_slots[id];

            Handle<T> result{};
            storageOfConst<T>(slot).forEach([&](uint32_t index, const T& res) {
                if (result) return;  // first match wins
                if (res.name == name) {
                    result = Handle<T>{StorageIndex{index, slot.allocator->generationOf(index)}};
                }
            });
            return result;
        }

        /**
         * @brief Visit every live resource of type T as (Handle<T>, const T&).
         *
         * Linear scan - for editor asset pickers / tooling, not hot paths.
         * A no-op when the type is unregistered.
         */
        template<typename T, typename Fn>
        void forEachOfType(Fn&& fn) const {
            TypeId id = typeId<T>();
            if (id >= m_slots.size() || !m_slots[id]) return;
            const auto& slot = *m_slots[id];
            storageOfConst<T>(slot).forEach([&](uint32_t index, const T& res) {
                fn(Handle<T>{StorageIndex{index, slot.allocator->generationOf(index)}}, res);
            });
        }

        /// @brief Global version counter, incremented on every commit().
        uint64_t getGlobalVersion() const { return m_globalVersion; }

        /**
         * @brief Drop every resource of every registered type.
         *
         * Used by scene-load flows that want a true cold-start (and by tests).
         * Does not check refcounts — caller is responsible for clearing the
         * scene first so no entity is still pointing at a freed handle.
         */
        void clear() {
            m_slots.clear();
            m_dependents.clear();
            ++m_globalVersion;
        }

        /// @brief Per-type version counter. Bumped only when resources of type T are committed.
        template<typename T>
        uint64_t getTypeVersion() const {
            TypeId id = typeId<T>();
            if (id >= m_slots.size() || !m_slots[id]) return 0;
            return m_slots[id]->typeVersion;
        }

        // --- Resource dependency tracking ---

        template<typename FromHandle, typename ToHandle>
        void addDependency(const FromHandle& from, const ToHandle& to) {
            uint64_t fromKey = packKey<typename FromHandle::resource_t>(from.key.index);
            uint64_t toKey   = packKey<typename ToHandle::resource_t>(to.key.index);
            m_dependents[toKey].push_back(fromKey);
        }

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

        template<typename HandleType>
        bool hasDependents(const HandleType& handle) const {
            uint64_t key = packKey<typename HandleType::resource_t>(handle.key.index);
            auto it = m_dependents.find(key);
            return it != m_dependents.end() && !it->second.empty();
        }

        template<typename HandleType>
        size_t dependentCount(const HandleType& handle) const {
            uint64_t key = packKey<typename HandleType::resource_t>(handle.key.index);
            auto it = m_dependents.find(key);
            return it != m_dependents.end() ? it->second.size() : 0;
        }

    private:
        /// Per-type bundle: lifetime, storage, refcounts, version.
        struct TypedSlot {
            std::unique_ptr<SlotAllocator>  allocator;
            std::unique_ptr<ISparseSet>     storage;
            std::vector<uint32_t>           refCounts;
            uint64_t                        typeVersion = 0;
        };

        template<typename T>
        TypedSlot& getSlot() {
            TypeId id = typeId<T>();
            if (id >= m_slots.size()) m_slots.resize(id + 1);
            if (!m_slots[id]) {
                m_slots[id] = std::make_unique<TypedSlot>();
                m_slots[id]->allocator = std::make_unique<SlotAllocator>();
                m_slots[id]->storage   = std::make_unique<SparseSet<T>>();
            }
            return *m_slots[id];
        }

        template<typename T>
        const TypedSlot& getSlotConst() const {
            TypeId id = typeId<T>();
            VKM_ASSERT(id < m_slots.size() && m_slots[id], "ResourceManager: type not registered");
            return *m_slots[id];
        }

        template<typename T>
        static SparseSet<T>& storageOf(TypedSlot& slot) {
            return static_cast<SparseSet<T>&>(*slot.storage);
        }

        template<typename T>
        static const SparseSet<T>& storageOfConst(const TypedSlot& slot) {
            return static_cast<const SparseSet<T>&>(*slot.storage);
        }

        /// Pack typeId + handle index into a single 64-bit key for the dependency graph.
        template<typename T>
        static uint64_t packKey(uint32_t index) {
            return (static_cast<uint64_t>(typeId<T>()) << 32) | index;
        }

    private:
        std::vector<std::unique_ptr<TypedSlot>> m_slots;
        uint64_t m_globalVersion = 0;

        /// Dependency graph: key = resource being depended on, value = list of resources that depend on it
        std::unordered_map<uint64_t, std::vector<uint64_t>> m_dependents;
};

} // namespace Engine
