#pragma once

#include <cstdint>

#include "core/memory/types.h"

namespace Engine {

/**
 * @brief Entity identifier backed by a generational StorageIndex.
 *
 * Pairs a sparse-array slot index with a generation counter, giving entities
 * the same use-after-free protection and ID recycling that resource handles
 * enjoy.
 *
 * Conceptually distinct from StorageIndex (cross-entity reference vs raw ECS
 * slot handle) but currently the same type. A newtype split is tracked in
 * docs/misc/gaps.md - deferred until a real second consumer of StorageIndex
 * appears outside this alias and Handle<T>.
 */
using EntityId = StorageIndex;

/**
 * @brief An Entity represents a unique object within the ECS architecture.
 *
 * Entities are identified solely by their EntityId (StorageIndex). They do not directly
 * own data or logic - instead, they are associated with components in storage. Entities
 * are lightweight handles, copyable and movable, and can be compared and checked for validity.
 */
class Entity {
    public:
        Entity() = default;
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
         * @brief Check if the entity is valid (has a nonzero index).
         * @return True if this entity references a valid slot; false if null.
         */
        constexpr explicit operator bool() const noexcept {
            return bool(m_id);
        }

        /**
         * @brief Equality comparison based on EntityId (index + generation).
         * @param other Entity to compare with.
         * @return True if both index and generation match.
         */
        constexpr bool operator==(const Entity& other) const noexcept {
            return m_id == other.m_id;
        }

        /**
         * @brief Get the underlying EntityId.
         * @return The EntityId (StorageIndex) for this entity.
         */
        EntityId getID() const { return m_id; }

    private:
        EntityId m_id{};
};

} // namespace Engine