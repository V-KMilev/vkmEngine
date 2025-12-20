#pragma once

#include <vector>
#include <optional>

#include "l_assert.h"
#include "entity.h"

namespace Engine {

/**
 * @brief A storage container for components of a given type T.
 * 
 * ComponentStorage provides efficient, type-safe storage and lookup of components for entities
 * via entity IDs. Each slot in the storage is either empty (std::nullopt) or contains a value of type T.
 * 
 * Design notes:
 * - The storage is implemented as a vector where the index corresponds to the EntityId.
 * - Adding a component requires the slot to be empty; overwriting is disallowed by an assertion.
 * - Removing a component resets the corresponding optional.
 * - Getting or checking a component is safe with assertion if out-of-range or missing.
 * - Access to the raw storage vector is provided for advanced use cases such as low-level iteration.
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
            VKM_ASSERT(!m_data[entity], "Component already exists on entity");
            m_data[entity] = std::move(component);
            return *m_data[entity];
        }

        /**
         * @brief Remove the component of type T for the given entity.
         * 
         * @param entity The entity's ID.
         */
        void remove(EntityId entity) {
            if (entity < m_data.size()) {
                m_data[entity].reset();
            }
        }

        /**
         * @brief Check if a component of type T exists for the given entity.
         * 
         * @param entity The entity's ID.
         * @return true if present, false otherwise.
         */
        bool has(EntityId entity) const {
            return entity < m_data.size() && m_data[entity].has_value();
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
            return *m_data[entity];
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
            return *m_data[entity];
        }

        /**
         * @brief Access the raw storage vector.
         * 
         * @return Const reference to the underlying vector of optionals (indexed by EntityId).
         */
         std::vector<std::optional<T>>& raw() { return m_data; }

        /**
         * @brief Access the raw storage vector.
         * 
         * @return Const reference to the underlying vector of optionals (indexed by EntityId).
         */
        const std::vector<std::optional<T>>& raw() const { return m_data; }

        /**
         * @brief Get the number of allocated slots in the storage.
         * 
         * This does not necessarily correspond to the number of active (non-empty) components.
         * @return The storage size.
         */
        size_t size() const { return m_data.size(); }

    private:
        /**
         * @brief Expand the storage to fit the given entity index, if needed.
         * 
         * @param entity The entity's ID.
         */
        void ensureSize(EntityId entity) {
            if (entity >= m_data.size())
                m_data.resize(entity + 1);
        }

    private:
        using value_type = T;
        std::vector<std::optional<T>> m_data;
};

} // namespace Engine