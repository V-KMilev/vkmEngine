#pragma once

#include <cstdint>
#include <vector>
#include <memory>

/**
 * @brief Forward declaration of Component class.
 */
class Component;

/**
 * @brief Enumeration representing possible types of entities.
 */
enum class EntityType {
    NONE = 0,
};

/**
 * @brief Convert an EntityType enum value to its string representation.
 * 
 * @param type The EntityType to convert.
 * @return const char* String representation of the EntityType.
 */
constexpr const char* toString(EntityType type) {
    switch (type) {
        case EntityType::NONE: return "NONE";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Represents an Entity in the system, containing an ID, type, and a set of components.
 */
class Entity {
    public:
        Entity() = delete;
        virtual ~Entity();

        Entity(const Entity& other) = delete;
        Entity& operator = (const Entity& other) = delete;

        Entity(Entity && other) noexcept = default;
        Entity& operator = (Entity && other) noexcept = default;

        /**
         * @brief Constructor for a single component.
         * @param id The unique identifier of the entity.
         * @param type The type of the entity.
         * @param component A component to add to the entity.
         */
         Entity(
            uint32_t id,
            EntityType type = EntityType::NONE
        );

        /**
         * @brief Constructor for a single component.
         * @param id The unique identifier of the entity.
         * @param type The type of the entity.
         * @param component A component to add to the entity.
         */
        Entity(
            uint32_t id,
            EntityType type,
            std::shared_ptr<Component> && component
        );

        /**
         * @brief Constructor for multiple components.
         * @param id The unique identifier of the entity.
         * @param type The type of the entity.
         * @param components List of components to add to the entity.
         */
        Entity(
            uint32_t id,
            EntityType type,
            std::vector<std::shared_ptr<Component>> && components
        );

    public:
        /**
         * @brief Get the unique ID of the entity.
         * @return uint32_t The entity's ID.
         */
        uint32_t getID() const;

        /**
         * @brief Get the type of the entity.
         * @return EntityType The entity's type.
         */
        EntityType getType() const;

        /**
         * @brief Get the list of components (modifiable).
         * @return Reference to the vector of component pointers.
         */
        std::vector<std::shared_ptr<Component>>& getComponents();

        /**
         * @brief Get the list of components (read-only).
         * @return Const reference to the vector of component pointers.
         */
        const std::vector<std::shared_ptr<Component>>& getComponents() const;

        /**
         * @brief Add a component to the entity.
         * @param component The component to add, passed as rvalue.
         */
        void addComponent(std::shared_ptr<Component> && component);

    protected:
        uint32_t m_id;
        EntityType m_type;

        std::vector<std::shared_ptr<Component>> m_components;
};
