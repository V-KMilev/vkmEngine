#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief Where a contact between two colliders is in its life, as of one live tick.
 *
 * One pair of entities touching. A pair whose trigger flags change while touching ends one contact and
 * begins another.
 */
enum class ContactPhase : uint8_t {
    Began,   ///< The first live tick the two touch.
    Stayed,  ///< A later tick they still touch, while either is awake and not static.
    Ended,   ///< The first live tick they no longer touch, or one of them is gone.
};

/**
 * @brief A resolved (non-trigger) contact between two bodies began, lasted or ended this tick.
 *
 * `a` has the lower entity slot, so a pair is named the same way every tick. On Began and Stayed,
 * `point` is a representative world-space position and `normal` points a to b; on Ended both keep their
 * defaults, and either id may name a dead entity, since Ended is sent whatever ended the contact.
 * See Behavior::onCollisionEnter for the per-entity form.
 */
struct CollisionEvent {
    EntityId     a;
    EntityId     b;
    ContactPhase phase  = ContactPhase::Began;
    glm::vec3    point  = {0.0f, 0.0f, 0.0f};
    glm::vec3    normal = {0.0f, 1.0f, 0.0f};
};

/**
 * @brief One entity's view of a CollisionEvent, as its behaviors' collision hooks receive it.
 *
 * `normal` points from `other` into the receiver (Unity's convention): a body landing on the floor
 * hears up, the floor hears down. On onCollisionExit `point` and `normal` are zero.
 */
struct Collision {
    EntityId  other;                            ///< The entity on the far side of the contact.
    glm::vec3 point  = {0.0f, 0.0f, 0.0f};      ///< A representative world-space contact position.
    glm::vec3 normal = {0.0f, 0.0f, 0.0f};      ///< Unit, from other into this entity.
};

/**
 * @brief Something began, kept or stopped overlapping a trigger collider this tick.
 *
 * One per trigger in a pair (two if both are triggers). Phases as CollisionEvent's, including Ended for
 * an entity destroyed inside. See Behavior::onTriggerEnter for the per-entity form.
 */
struct TriggerEvent {
    EntityId     trigger;  ///< The entity whose collider isTrigger.
    EntityId     other;    ///< The collider it overlapped.
    ContactPhase phase = ContactPhase::Began;
};

} // namespace Vkm::Engine
