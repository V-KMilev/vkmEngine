#pragma once

#include <cstdint>
#include <memory>

#include <vector>
#include <unordered_map>

class Component;
enum class ComponentType;

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
        Entity& operator=(const Entity& other) = delete;

        Entity(Entity && other) noexcept = default;
        Entity& operator=(Entity && other) noexcept = default;

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
         * @param components Vector of components to add to the entity.
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
         * @brief Get a component by type (O(1) lookup).
         * @param type The ComponentType to look for.
         * @return std::shared_ptr<Component> The component of the given type, or nullptr if not found.
         */
        std::shared_ptr<Component> getComponent(ComponentType type) const;

        /**
         * @brief Get a component by type, cast to T (O(1) lookup).
         * @tparam T The component class to cast to.
         * @param type The ComponentType to look for.
         * @return std::shared_ptr<T> The component instance cast to T, or nullptr if not found or cast fails.
         */
        template <typename T>
        std::shared_ptr<T> getComponentAs(ComponentType type) const {
            auto base = getComponent(type);
            if (!base) {
                return nullptr;
            }
            return std::static_pointer_cast<T>(base);
        }

        /**
         * @brief Check if the entity has a component of the given type.
         * @param type The ComponentType to check for.
         * @return bool True if the component exists, false otherwise.
         */
        bool hasComponent(ComponentType type) const;

        /**
         * @brief Add a component to the entity.
         * @param component The component to add, passed as rvalue.
         */
        void addComponent(std::shared_ptr<Component> && component);

    protected:
        uint32_t m_id;
        EntityType m_type;

        std::unordered_map<ComponentType, std::shared_ptr<Component>> m_components;
};
