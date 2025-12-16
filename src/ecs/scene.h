#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <type_traits>
#include <utility>

#include "entity.h"
#include "component.h"

namespace Engine {

/**
 * @brief The Scene class manages a collection of entities and their components within the engine.
 * 
 * Provides APIs for entity and component creation, and utilities for component lookup.
 * The Scene is the top-level owner of all entities and is responsible for generating unique IDs
 * for both entities and components. Entities are stored by value in a std::deque, ensuring stable 
 * references/pointers throughout their lifetime (as deque does not invalidate references or pointers 
 * on insertion).
 * 
 * This class is non-copyable and non-movable to guarantee ownership and ID stability.
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
         * @brief Create an entity (owned by the scene) and return a stable reference.
         *
         * Entities are stored by value, so their memory addresses don't change after insertion.
         * This method is useful if you only need an entity without any initial components.
         *
         * @param type The EntityType for the new entity (default is EntityType::NONE).
         * @return Reference to the newly created Entity.
         */
        Entity& createEntity(EntityType type = EntityType::NONE);

        /**
         * @brief Create an entity with one component.
         *
         * This method allows you to create an entity and immediately associate it with a single component.
         *
         * @param type The EntityType for the new entity.
         * @param component A unique pointer to the component to attach to the entity.
         * @return Reference to the newly created Entity.
         */
        Entity& createEntity(EntityType type, std::shared_ptr<Component>&& component);

        /**
         * @brief Create an entity with multiple components.
         *
         * This method allows you to create an entity and immediately associate it with a list of components.
         *
         * @param type The EntityType for the new entity.
         * @param components A vector of unique component pointers to attach to the entity.
         * @return Reference to the newly created Entity.
         */
        Entity& createEntity(EntityType type, std::vector<std::shared_ptr<Component>>&& components);

        /**
         * @brief Create a component of type T with a unique ID managed by the scene.
         * 
         * This utility constructs a component derived from Component and assigns it a unique ID inside the scene.
         * @tparam T Component type deriving from Component.
         * @tparam Args Constructor arguments for T, excluding the ID (which is automatically generated).
         * @param args Arguments to forward to the component's constructor.
         * @return std::shared_ptr<T> Owning pointer to the created component.
         * 
         * @note T must publicly derive from Component.
         */
        template <typename T, typename... Args>
        std::shared_ptr<T> createComponent(Args&&... args) {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            return std::make_shared<T>(m_componentId++, std::forward<Args>(args)...);
        }

        /**
         * @brief Access entities (modifiable).
         *
         * @return Reference to the std::deque containing entities, allowing modification.
         */
        std::deque<Entity>& getEntities() { return m_entities; }

        /**
         * @brief Access entities (read-only).
         *
         * @return Const reference to the std::deque containing entities.
         */
        const std::deque<Entity>& getEntities() const { return m_entities; }

    public:
        /**
         * @brief Find the first component of the specified type for the given entity.
         * 
         * Iterates through the entity's components and returns the first matching the specified ComponentType.
         *
         * @param entity The entity to search for the component.
         * @param type The ComponentType to look for.
         * @return std::shared_ptr<Component> The first component of the given type, or nullptr if not found.
         */
        static std::shared_ptr<Component> findComponent(const Entity& entity, ComponentType type) {
            for (const auto& component : entity.getComponents()) {
                if (component && component->getType() == type) return component;
            }
            return nullptr;
        }

        /**
         * @brief Find the first component of a specified type, cast to T, for the given entity.
         * 
         * If a component of the desired type is found, returns a std::shared_ptr<T>, otherwise returns nullptr.
         * Uses dynamic_pointer_cast for safety.
         *
         * @tparam T The component class to cast to.
         * @param entity The entity to search for the component.
         * @param type The ComponentType to look for.
         * @return std::shared_ptr<T> The component instance cast to T, or nullptr if not found or cast fails.
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
        uint32_t m_entityId = 1;
        uint32_t m_componentId = 1;

        std::deque<Entity> m_entities;
};

} // namespace Engine