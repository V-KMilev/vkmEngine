#pragma once

#include <memory>
#include <type_traits>
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
 * a SparseSet<T> (storage), and a typeVersion counter.
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

            return Handle<T>{key};
        }

        /// @brief Convenience overload: stamp a name onto the asset before insertion.
        template<typename ResourceType>
        auto add(ResourceType && resource, std::string name) {
            resource.name = std::move(name);
            return add(std::forward<ResourceType>(resource));
        }

        /**
         * @brief Insert an editor-internal asset (preview primitives,
         *        neutral thumbnail materials, etc.).
         *
         * Pickers, the Asset Browser and the scene saver all filter on
         * Resource::internal so these never surface to the user or get
         * serialized into a scene save.
         *
         * @tparam ResourceType The resource type (must inherit from Resource).
         * @param resource The resource instance to add (will be moved).
         * @param name Stable name to stamp onto the asset.
         * @return Handle for the newly inserted internal asset.
         */
        template<typename ResourceType>
        auto addInternal(ResourceType && resource, std::string name) {
            resource.internal = true;
            resource.name = std::move(name);
            return add(std::forward<ResourceType>(resource));
        }

        /**
         * @brief Remove a resource by handle.
         */
        template<typename HandleType>
        void remove(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::remove invalid handle");
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
            ++m_globalVersion;
        }

        /// @brief Per-type version counter. Bumped only when resources of type T are committed.
        template<typename T>
        uint64_t getTypeVersion() const {
            TypeId id = typeId<T>();
            if (id >= m_slots.size() || !m_slots[id]) return 0;
            return m_slots[id]->typeVersion;
        }

    private:
        /// Per-type bundle: lifetime, storage, version.
        struct TypedSlot {
            std::unique_ptr<SlotAllocator>  allocator;
            std::unique_ptr<ISparseSet>     storage;
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

    private:
        std::vector<std::unique_ptr<TypedSlot>> m_slots;
        uint64_t m_globalVersion = 0;
};

} // namespace Engine
