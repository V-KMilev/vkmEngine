#pragma once

#include <memory>
#include <tuple>
#include <vector>

#include "entity.h"
#include "slot_allocator.h"
#include "sparse_set.h"
#include "types.h"

namespace Engine {

/**
 * @brief Central registry managing entities and an open set of component types.
 *
 * Scene provides efficient creation, component assignment, lookup, and removal
 * for entities. Entity lifetime is managed by a SlotAllocator (generation-safe
 * handles with recycling). Component data is stored in type-erased SparseSet<T>
 * containers that are created on first use — any type can be a component without
 * modifying Scene.
 */
class Scene {
    public:
        Scene() = default;
        ~Scene() = default;

        Scene(const Scene& other) = delete;
        Scene& operator=(const Scene& other) = delete;

        Scene(Scene && other) = delete;
        Scene& operator=(Scene && other) = delete;

    public:
        /**
         * @brief Create a new entity and assign a unique EntityId.
         * @return The created Entity.
         */
        Entity createEntity() {
            StorageIndex id = m_entityAllocator.allocate();
            return Entity{id};
        }

        /**
         * @brief Destroy an entity by removing all of its components and recycling its slot.
         * @param entity The entity to destroy.
         */
        void destroyEntity(Entity entity) {
            EntityId id = entity.getID();
            for (auto& set : m_components) {
                if (set && set->has(id.index))
                    set->remove(id.index);
            }
            m_entityAllocator.free(id);
        }

        /**
         * @brief Add a component to an entity.
         * @tparam T Component type (any type; storage is created on first use).
         * @param entity The entity to add the component to.
         * @param component The component instance to add.
         * @return Reference to the added component in storage.
         */
        template<typename T>
        auto& add(Entity entity, T && component) {
            using U = std::remove_cv_t<std::remove_reference_t<T>>;
            return getStorage<U>().add(entity.getID().index, std::forward<T>(component));
        }

        /**
         * @brief Check if an entity has a component of type T.
         * @tparam T Component type.
         * @param entity Entity or EntityId to query.
         * @return true if the component exists for the entity, false otherwise.
         */
        template<typename T>
        bool has(Entity entity) const { return has<T>(entity.getID()); }

        template<typename T>
        bool has(EntityId entity) const {
            auto* store = findStorage<T>();
            return store && store->has(entity.index);
        }

        /**
         * @brief Get a mutable reference to an entity's component of type T.
         * @tparam T Component type.
         * @param entity Entity or EntityId from which to get the component.
         * @return Reference to the component.
         */
        template<typename T>
        T& get(Entity entity) { return get<T>(entity.getID()); }

        template<typename T>
        T& get(EntityId entity) {
            return getStorage<T>().get(entity.index);
        }

        /**
         * @brief Get a const reference to an entity's component of type T.
         * @tparam T Component type.
         * @param entity Entity or EntityId from which to get the component.
         * @return Const reference to the component.
         */
        template<typename T>
        const T& get(Entity entity) const { return get<T>(entity.getID()); }

        template<typename T>
        const T& get(EntityId entity) const {
            auto* store = findStorage<T>();
            VKM_ASSERT(store, "Scene::get called for unregistered component type");
            return store->get(entity.index);
        }

        /**
         * @brief Remove a component of type T from an entity.
         * @tparam T Component type.
         * @param entity The entity whose component will be removed.
         */
        template<typename T>
        void remove(Entity entity) {
            auto* store = findStorage<T>();
            if (store && store->has(entity.getID().index))
                store->remove(entity.getID().index);
        }

        /**
         * @brief Number of live components of type T.
         * @tparam T Component type.
         */
        template<typename T>
        size_t count() const {
            auto* store = findStorage<T>();
            return store ? store->size() : 0;
        }

        /**
         * @brief Iterate all live components densely (no holes).
         *
         * With a single type, calls fn(EntityId, First&) for each live component.
         * With multiple types, iterates First and yields only entities that also
         * have all Rest types. Put the rarest component type first.
         *
         * @tparam First Primary component type (iterated).
         * @tparam Rest  Additional required component types (checked per entity).
         * @param fn Callable with signature void(EntityId, First&, Rest&...).
         */
        template<typename First, typename... Rest, typename Fn>
        void forEach(Fn&& fn) {
            auto& firstStorage = getStorage<First>();

            if constexpr (sizeof...(Rest) == 0) {
                firstStorage.forEach([&](uint32_t entityIdx, First& first) {
                    EntityId eid{entityIdx, m_entityAllocator.generationOf(entityIdx)};
                    fn(eid, first);
                });
            } else {
                auto restStorages = std::make_tuple(&getStorage<Rest>()...);

                firstStorage.forEach([&](uint32_t entityIdx, First& first) {
                    if (!(std::get<SparseSet<Rest>*>(restStorages)->has(entityIdx) && ...)) return;

                    EntityId eid{entityIdx, m_entityAllocator.generationOf(entityIdx)};
                    fn(eid, first, std::get<SparseSet<Rest>*>(restStorages)->get(entityIdx)...);
                });
            }
        }

        template<typename First, typename... Rest, typename Fn>
        void forEach(Fn&& fn) const {
            auto* firstStorage = findStorage<First>();
            if (!firstStorage) return;

            if constexpr (sizeof...(Rest) == 0) {
                firstStorage->forEach([&](uint32_t entityIdx, const First& first) {
                    EntityId eid{entityIdx, m_entityAllocator.generationOf(entityIdx)};
                    fn(eid, first);
                });
            } else {
                auto restStorages = std::make_tuple(findStorage<Rest>()...);
                if (!(std::get<const SparseSet<Rest>*>(restStorages) && ...)) return;

                firstStorage->forEach([&](uint32_t entityIdx, const First& first) {
                    if (!(std::get<const SparseSet<Rest>*>(restStorages)->has(entityIdx) && ...)) return;

                    EntityId eid{entityIdx, m_entityAllocator.generationOf(entityIdx)};
                    fn(eid, first, std::get<const SparseSet<Rest>*>(restStorages)->get(entityIdx)...);
                });
            }
        }

    private:
        /// @brief Get or create the typed SparseSet for component type T.
        template<typename T>
        SparseSet<T>& getStorage() {
            TypeId id = typeId<T>();
            if (id >= m_components.size())
                m_components.resize(id + 1);
            auto& ptr = m_components[id];
            if (!ptr) ptr = std::make_unique<SparseSet<T>>();
            return static_cast<SparseSet<T>&>(*ptr);
        }

        /// @brief Find the typed SparseSet for component type T, or nullptr if unregistered.
        template<typename T>
        const SparseSet<T>* findStorage() const {
            TypeId id = typeId<T>();
            if (id >= m_components.size() || !m_components[id]) return nullptr;
            return static_cast<const SparseSet<T>*>(m_components[id].get());
        }

        /// @brief Non-const findStorage for mutable access without lazy creation.
        template<typename T>
        SparseSet<T>* findStorage() {
            TypeId id = typeId<T>();
            if (id >= m_components.size() || !m_components[id]) return nullptr;
            return static_cast<SparseSet<T>*>(m_components[id].get());
        }

    private:
        SlotAllocator m_entityAllocator;
        std::vector<std::unique_ptr<ISparseSet>> m_components;
};

} // namespace Engine
