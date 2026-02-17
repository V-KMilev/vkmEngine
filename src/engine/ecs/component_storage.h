#pragma once

#include <vector>

#include "l_assert.h"
#include "entity.h"
#include "my_storage.h"

namespace Engine {

/**
 * @brief A storage container for components of a given type T, backed by Engine::Storage (slot map).
 *
 * ComponentStorage provides efficient, type-safe storage and lookup of components for entities
 * via entity IDs. Internally delegates to Engine::Storage<T> for O(1) add/remove/lookup with
 * dense packed data, while maintaining an EntityId-to-StorageIndex mapping for entity-based access.
 *
 * - Adding a component requires the slot to be empty; overwriting is disallowed by an assertion.
 * - Removing a component is O(1) via swap-and-pop in the dense array.
 * - size() returns the entity map capacity (max EntityId + 1), not the number of live components.
 * - forEach() iterates only live components (dense, no holes) with their EntityIds.
 *
 * All special member functions are disabled except default construction/destruction.
 */
template<typename T>
class ComponentStorage {
    public:
        ComponentStorage() = default;
        ~ComponentStorage() = default;

        ComponentStorage(const ComponentStorage& other) = delete;
        ComponentStorage& operator=(const ComponentStorage& other) = delete;

        ComponentStorage(ComponentStorage && other) = delete;
        ComponentStorage& operator=(ComponentStorage && other) = delete;

    public:
        /**
         * @brief Add a component instance for a given entity.
         *
         * @param entity The entity's ID.
         * @param component The component instance (moved in).
         * @return Reference to the added component.
         * @throws Assertion failure if the entity already owns a component of type T.
         */
        T& add(EntityId entity, T && component) {
            ensureSize(entity);
            VKM_ASSERT(!has(entity), "Component already exists on entity");
            StorageIndex key = m_storage.add(std::move(component));
            m_entityMap[entity] = key;

            if (key.index >= m_slotToEntity.size())
                m_slotToEntity.resize(key.index + 1);
            m_slotToEntity[key.index] = entity;

            return m_storage.get(key);
        }

        /**
         * @brief Remove the component of type T for the given entity.
         *
         * @param entity The entity's ID.
         */
        void remove(EntityId entity) {
            if (entity < m_entityMap.size() && m_storage.has(m_entityMap[entity])) {
                m_storage.remove(m_entityMap[entity]);
                m_entityMap[entity] = {};
            }
        }

        /**
         * @brief Check if a component of type T exists for the given entity.
         *
         * @param entity The entity's ID.
         * @return true if present, false otherwise.
         */
        bool has(EntityId entity) const {
            return entity < m_entityMap.size() && m_storage.has(m_entityMap[entity]);
        }

        /**
         * @brief Access a mutable reference to a component.
         *
         * @param entity The entity's ID.
         * @return Reference to the component.
         * @throws Assertion failure if the component does not exist.
         */
        T& get(EntityId entity) {
            VKM_ASSERT(has(entity), "Component does not exist on entity");
            return m_storage.get(m_entityMap[entity]);
        }

        /**
         * @brief Access a const reference to a component.
         *
         * @param entity The entity's ID.
         * @return Const reference to the component.
         * @throws Assertion failure if the component does not exist.
         */
        const T& get(EntityId entity) const {
            VKM_ASSERT(has(entity), "Component does not exist on entity");
            return m_storage.get(m_entityMap[entity]);
        }

        /**
         * @brief Get the number of allocated entity slots.
         *
         * Returns the entity map capacity (max EntityId + 1), not the number of live components.
         * Used as iteration bound: for (EntityId id = 0; id < storage.size(); ++id)
         * @return The entity map size.
         */
        size_t size() const { return m_entityMap.size(); }

        /**
         * @brief Number of live components (dense array size).
         */
        size_t count() const { return m_storage.size(); }

        /**
         * @brief Iterate all live components densely (no holes).
         *
         * Calls fn(EntityId, T&) for each live component, iterating the packed dense
         * array directly. Much faster than sparse EntityId iteration when there are gaps.
         *
         * @param fn Callable with signature void(EntityId, T&).
         */
        template<typename Fn>
        void forEach(Fn&& fn) {
            T* dense = m_storage.data();
            for (uint32_t i = 0; i < m_storage.size(); ++i) {
                uint32_t sparseSlot = m_storage.keyAt(i).index;
                fn(m_slotToEntity[sparseSlot], dense[i]);
            }
        }

        /**
         * @brief Iterate all live components densely (no holes), const version.
         *
         * Calls fn(EntityId, const T&) for each live component.
         *
         * @param fn Callable with signature void(EntityId, const T&).
         */
        template<typename Fn>
        void forEach(Fn&& fn) const {
            const T* dense = m_storage.data();
            for (uint32_t i = 0; i < m_storage.size(); ++i) {
                uint32_t sparseSlot = m_storage.keyAt(i).index;
                fn(m_slotToEntity[sparseSlot], dense[i]);
            }
        }

    private:
        /**
         * @brief Expand the entity map to fit the given entity index, if needed.
         *
         * @param entity The entity's ID.
         */
        void ensureSize(EntityId entity) {
            if (entity >= m_entityMap.size())
                m_entityMap.resize(entity + 1);
        }

    private:
        Storage<T> m_storage;                    ///< Dense packed component data (slot map)
        std::vector<StorageIndex> m_entityMap;   ///< EntityId -> StorageIndex mapping
        std::vector<EntityId> m_slotToEntity;    ///< Sparse slot -> EntityId reverse mapping
};

} // namespace Engine
