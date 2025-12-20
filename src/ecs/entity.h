#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief Strong type for uniquely identifying entities in the ECS.
 */
using EntityId = uint32_t;

/**
 * @brief An Entity represents a unique object within the ECS architecture.
 *
 * Entities are identified solely by their EntityId. They do not directly own data or logic—
 * instead, they are associated with components in storage. Entities are lightweight handles,
 * copyable and movable, and can be compared and checked for validity.
 */
class Entity {
    public:
        Entity() = delete;
        ~Entity() = default;

        Entity(const Entity& other) noexcept = default;
        Entity& operator=(const Entity& other) noexcept = default;

        Entity(Entity && other) noexcept = default;
        Entity& operator=(Entity && other) noexcept = default;

        /**
         * @brief Construct an Entity with the given EntityId.
         * @param id The unique EntityId assigned to this entity.
         */
        explicit Entity(EntityId id) : m_id(id) {}

    public:
        /**
         * @brief Check if the entity is valid (has a nonzero id).
         * @return True if this entity was assigned a nonzero id; false if default constructed.
         */
        constexpr explicit operator bool() const noexcept {
            return m_id != 0;
        }

        /**
         * @brief Equality comparison based on EntityId.
         * @param other Entity to compare with.
         * @return True if the two entities have the same id.
         */
        constexpr bool operator==(const Entity& other) const noexcept {
            return m_id == other.m_id;
        }

        /**
         * @brief Get the underlying EntityId.
         * @return The raw EntityId.
         */
        EntityId getID() const { return m_id; }

    private:
        EntityId m_id;
};

} // namespace Engine