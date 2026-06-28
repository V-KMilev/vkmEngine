#pragma once

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Engine {

/**
 * @brief A resolved (non-trigger) contact between two physics bodies this tick.
 *
 * Emitted by PhysicsSystem once per overlapping body pair per fixed tick (while
 * the overlap lasts - no enter/exit phases yet). `normal` points from a toward
 * b; `point` is a representative world-space contact position. Behaviors get
 * this as onCollision(other); other systems may subscribe directly.
 */
struct CollisionEvent {
    EntityId  a;
    EntityId  b;
    glm::vec3 point  = {0.0f, 0.0f, 0.0f};
    glm::vec3 normal = {0.0f, 1.0f, 0.0f};
};

/**
 * @brief A trigger collider overlapped another collider this tick.
 *
 * Emitted per trigger in an overlapping pair (if both are triggers, two events),
 * so the trigger's owner learns who entered it. Delivered to behaviors as
 * onTrigger(other). Like CollisionEvent, this fires while overlapping, every
 * fixed tick - no enter/exit edge detection yet.
 */
struct TriggerEvent {
    EntityId trigger;  ///< The entity whose collider isTrigger.
    EntityId other;    ///< The collider it overlapped.
};

} // namespace Engine
