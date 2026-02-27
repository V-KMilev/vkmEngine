#pragma once

#include <memory>
#include <tuple>
#include <vector>

#include "ecs/entity.h"
#include "ecs/component/hierarchy.h"
#include "core/memory/slot_allocator.h"
#include "core/memory/sparse_set.h"
#include "core/memory/types.h"
#include "debug/statistics.h"

namespace Engine {

/**
 * @brief Central registry managing entities and an open set of component types.
 *
 * Scene provides efficient creation, component assignment, lookup, and removal
 * for entities. Entity lifetime is managed by a SlotAllocator (generation-safe
 * handles with recycling). Component data is stored in type-erased SparseSet<T>
 * containers that are created on first use - any type can be a component without
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
            if (id.index >= m_componentMasks.size())
                m_componentMasks.resize(id.index + 1, 0);
            m_componentMasks[id.index] = 0;
            STATS_RECORD_ENTITY_CREATE();
            return Entity{id};
        }

        /**
         * @brief Check if an entity is still alive (valid index + matching generation).
         * @param entity The entity to check.
         * @return true if the entity is alive.
         */
        bool isAlive(Entity entity) const { return isAlive(entity.getID()); }
        bool isAlive(EntityId id) const { return m_entityAllocator.has(id); }

        /**
         * @brief Number of live entities.
         */
        size_t entityCount() const { return m_entityAllocator.size(); }

        /**
         * @brief Get the generation counter for an entity index (for reconstructing EntityId from dense index).
         */
        uint32_t generationOf(uint32_t index) const { return m_entityAllocator.generationOf(index); }

        /**
         * @brief Destroy an entity by removing all of its components and recycling its slot.
         * @param entity The entity to destroy.
         */
        void destroyEntity(Entity entity) {
            EntityId id = entity.getID();

            // Clean up hierarchy: reparent children to grandparent, unlink from parent
            auto* hierarchySet = findStorage<Hierarchy>();
            if (hierarchySet && hierarchySet->contains(id.index)) {
                Hierarchy& h = hierarchySet->get(id.index);

                // Reparent children to this entity's parent (detach if root)
                EntityId child = h.firstChild;
                while (child) {
                    Hierarchy& childH = hierarchySet->get(child.index);
                    EntityId nextChild = childH.nextSibling;

                    childH.prevSibling = {};
                    childH.nextSibling = {};
                    childH.parent = h.parent;

                    if (h.parent) {
                        Hierarchy& parentH = hierarchySet->get(h.parent.index);
                        childH.nextSibling = parentH.firstChild;
                        if (parentH.firstChild) {
                            hierarchySet->get(parentH.firstChild.index).prevSibling = child;
                        }
                        parentH.firstChild = child;
                    }

                    child = nextChild;
                }

                // Unlink from parent's child list
                if (h.parent) {
                    if (h.prevSibling) {
                        hierarchySet->get(h.prevSibling.index).nextSibling = h.nextSibling;
                    } else {
                        hierarchySet->get(h.parent.index).firstChild = h.nextSibling;
                    }
                    if (h.nextSibling) {
                        hierarchySet->get(h.nextSibling.index).prevSibling = h.prevSibling;
                    }
                }
            }

            uint64_t mask = m_componentMasks[id.index];
            while (mask) {
                uint32_t bit = static_cast<uint32_t>(__builtin_ctzll(mask));
                if (bit < m_components.size() && m_components[bit])
                    m_components[bit]->remove(id.index);
                mask &= mask - 1;
            }
            m_componentMasks[id.index] = 0;
            m_entityAllocator.free(id);
            STATS_RECORD_ENTITY_DESTROY();
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
            VKM_ASSERT(isAlive(entity), "Scene::add called with dead/stale entity");
            using U = std::remove_cv_t<std::remove_reference_t<T>>;
            auto& result = getStorage<U>().add(entity.getID().index, std::forward<T>(component));
            TypeId tid = typeId<U>();
            VKM_ASSERT(tid < 64, "Component type count exceeds 64-bit mask capacity");
            m_componentMasks[entity.getID().index] |= (uint64_t(1) << tid);
            return result;
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
            VKM_ASSERT(isAlive(entity), "Scene::has called with dead/stale entity");
            auto* store = findStorage<T>();
            return store && store->contains(entity.index);
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
            VKM_ASSERT(isAlive(entity), "Scene::get called with dead/stale entity");
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
            VKM_ASSERT(isAlive(entity), "Scene::get called with dead/stale entity");
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
            VKM_ASSERT(isAlive(entity), "Scene::remove called with dead/stale entity");
            auto* store = findStorage<T>();
            if (store && store->has(entity.getID().index)) {
                store->remove(entity.getID().index);
                TypeId tid = typeId<T>();
                m_componentMasks[entity.getID().index] &= ~(uint64_t(1) << tid);
            }
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
                    if (!(std::get<SparseSet<Rest>*>(restStorages)->contains(entityIdx) && ...)) return;

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
                    if (!(std::get<const SparseSet<Rest>*>(restStorages)->contains(entityIdx) && ...)) return;

                    EntityId eid{entityIdx, m_entityAllocator.generationOf(entityIdx)};
                    fn(eid, first, std::get<const SparseSet<Rest>*>(restStorages)->get(entityIdx)...);
                });
            }
        }

        /**
         * @brief Direct access to the SparseSet for component type T (for index-based iteration).
         * @tparam T Component type.
         * @return Pointer to the SparseSet, or nullptr if unregistered.
         */
        /// @brief Compact all component sparse arrays to reclaim wasted memory.
        void compact() {
            for (auto& set : m_components) {
                if (set) set->compact();
            }
        }

        template<typename T>
        SparseSet<T>* storage() { return findStorage<T>(); }

        template<typename T>
        const SparseSet<T>* storage() const { return findStorage<T>(); }

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
        std::vector<uint64_t> m_componentMasks; ///< Per-entity bitmask of attached component types (indexed by entity index)
};

} // namespace Engine
