#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "l_assert.h"

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
 * `point` is a representative world-space position, `normal` points a to b and `speed` is how fast the
 * two closed along it as the tick began - how hard it struck, on Began; on Ended all three keep their
 * defaults, and either id may name a dead entity, since Ended is sent whatever ended the contact.
 * See Behavior::onCollisionEnter for the per-entity form.
 */
struct CollisionEvent {
    EntityId     a;
    EntityId     b;
    ContactPhase phase  = ContactPhase::Began;
    glm::vec3    point  = {0.0f, 0.0f, 0.0f};
    glm::vec3    normal = {0.0f, 1.0f, 0.0f};
    float        speed  = 0.0f;  ///< m/s, never below zero.
};

/**
 * @brief One entity's view of a CollisionEvent, as its behaviors' collision hooks receive it.
 *
 * `normal` points from `other` into the receiver (Unity's convention): a body landing on the floor
 * hears up, the floor hears down. On onCollisionExit `point`, `normal` and `speed` are zero.
 */
struct Collision {
    EntityId  other;                            ///< The entity on the far side of the contact.
    glm::vec3 point  = {0.0f, 0.0f, 0.0f};      ///< A representative world-space contact position.
    glm::vec3 normal = {0.0f, 0.0f, 0.0f};      ///< Unit, from other into this entity.
    float     speed  = 0.0f;                    ///< How fast the two closed, m/s: how hard it struck.

    /**
     * @brief @p event as @p self sees it: what a collision hook is handed, for a listener on the bus.
     *
     * @param event A contact @p self is in.
     * @param self  One of the pair: its `a` or its `b`.
     * @return The other, the point, and the normal into @p self; on Ended only the other.
     */
    static Collision seenBy(const CollisionEvent& event, EntityId self) {
        VKM_ASSERT(event.a == self || event.b == self, "Collision::seenBy: self is not one of the pair");
        const bool isA = event.a == self;
        Collision hit;
        hit.other = isA ? event.b : event.a;
        if (event.phase == ContactPhase::Ended) return hit;
        hit.point  = event.point;
        hit.normal = isA ? -event.normal : event.normal;
        hit.speed  = event.speed;
        return hit;
    }
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
