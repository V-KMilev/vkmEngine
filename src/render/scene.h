#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "entity.h"
#include "component.h"

namespace Engine {

/**
 * @brief Scene container that manages entities and their components.
 *
 * Provides API for entity creation (optionally with components), iteration, and type-safe component lookup.
 * Prevents copy/move semantics for safety and maintains an internal entity ID counter.
 */
class Scene {
    public:
        Scene();
        ~Scene() = default;

        Scene(const Scene& other) = delete;
        Scene& operator=(const Scene& other) = delete;

        Scene(Scene && other) = delete;
        Scene& operator=(Scene && other) = delete;

    public:
        /**
         * @brief Create an empty entity with an optional entity type.
         * @param type Type for entity (default EntityType::NONE).
         * @return Shared pointer to the created entity.
         */
         std::shared_ptr<Entity> createEntity(EntityType type = EntityType::NONE);

        /**
         * @brief Create an entity with a single component.
         * @param type Type for entity.
         * @param component Component to attach (move).
         * @return Shared pointer to the created entity.
         */
         std::shared_ptr<Entity> createEntity(EntityType type, std::shared_ptr<Component> && component);

        /**
         * @brief Create an entity with a list of components.
         * @param type Type for entity.
         * @param components Vector of components to attach (move).
         * @return Shared pointer to the created entity.
         */
         std::shared_ptr<Entity> createEntity(EntityType type, std::vector<std::shared_ptr<Component>> && components);

        /**
         * @brief Create a component of type T with a unique ID managed by the scene.
         * @tparam T Component type deriving from Component.
         * @tparam Args Constructor argument types for T (excluding the ID).
         * @param args Arguments forwarded to the component constructor after the generated ID.
         * @return Shared pointer to the created component.
         */
        template <typename T, typename... Args>
        std::shared_ptr<T> createComponent(Args&&... args) {
            static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
            return std::make_shared<T>(m_componentId++, std::forward<Args>(args)...);
        }

        /**
         * @brief Get mutable reference to all entities managed by the scene.
         * @return Reference to a vector of shared entity pointers.
         */
        std::vector<std::shared_ptr<Entity>>& getEntities() { return m_entities; }

        /**
         * @brief Get const reference to all entities managed by the scene.
         * @return Const reference to a vector of shared entity pointers.
         */
        const std::vector<std::shared_ptr<Entity>>& getEntities() const { return m_entities; }

        /**
         * @brief Find a component of a specific type attached to an entity.
         * @param entity Entity to search in.
         * @param type Desired component type.
         * @return Shared pointer to the found base component, or nullptr if none.
         */
        static std::shared_ptr<Component> findComponent(const Entity& entity, ComponentType type);

        /**
         * @brief Find and cast a component of a specific type to a given derived type.
         * @tparam T The derived component type to cast to.
         * @param entity Entity to search in.
         * @param type Desired component type.
         * @return Shared pointer to the component cast as type T, or nullptr if not found/cast fails.
         */
        template <typename T>
        static std::shared_ptr<T> findComponentAs(const Entity& entity, ComponentType type) {
            auto base = findComponent(entity, type);
            if (!base) {
                return nullptr;
            }

            return std::static_pointer_cast<T>(base);
        }

    private:
        uint32_t m_entityId;
        uint32_t m_componentId;
        std::vector<std::shared_ptr<Entity>> m_entities;
};

} // namespace Engine