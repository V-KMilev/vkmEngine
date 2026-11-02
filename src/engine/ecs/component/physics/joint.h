#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vkm_log/assert.h"
#include "vkm_physics/joint.h"

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief What a joint holds fixed between two bodies.
 *
 * Serialized by name, so reordering keeps scenes valid.
 */
enum class JointType : uint8_t {
    Point    = 0,  ///< Anchors coincide; rotation is free. A ball and socket.
    Distance = 1,  ///< Anchors stay a set distance apart. A rod: it pushes as well as pulls.
    Count          ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief @p type as the physics reads it.
 *
 * @param type A Joint's type.
 * @return The physics' joint type.
 */
inline Physics::JointType physicsJointTypeOf(JointType type) {
    switch (type) {
        case JointType::Point:    return Physics::JointType::Point;
        case JointType::Distance: return Physics::JointType::Distance;
        case JointType::Count:    break;
    }
    VKM_ASSERT(false, "Count is no joint type");
    return Physics::JointType::Point;
}

/**
 * @brief Ties this entity's body to another, at a point on each.
 *
 * A constraint, solved in the contact passes - each joint just before the contacts, so a surface has the
 * last word - so a jointed body stacks, sleeps and rests like any other. Anchors are in each body's own
 * frame. A `connected` entity with no Rigidbody holds the body to a fixed point in the world.
 */
struct Joint {
    JointType type = JointType::Point;

    EntityId  connected{};                           ///< The other body; empty, or this entity, disables
    glm::vec3 anchor          = {0.0f, 0.0f, 0.0f};  ///< On this body, local
    glm::vec3 connectedAnchor = {0.0f, 0.0f, 0.0f};  ///< On the other, local

    float distance = -1.0f;  ///< Distance the anchors are held at, in meters, for a Distance joint. Negative
                             ///< means "whatever they were when the joint was made", resolved on the first
                             ///< tick.

    float resolvedDistance = -1.0f;  ///< The length actually held, once the solver has measured it; runtime,
                                     ///< not serialized. Keeps `distance` as authored, including a negative
                                     ///< that asks for a measurement.

    float stiffness = 1.0f;  ///< How hard the joint pulls drifted anchors back together, 0 to 1: it scales
                             ///< the target the solver converges on, not a spring constant or a pass's
                             ///< impulse, so it cannot be tuned into instability or change meaning with the
                             ///< iteration count. At 1 drift closes overdamped at the joint spring's full
                             ///< rate; at 0 relative motion is still held but drift is left.

    float holdTorque = 0.0f;  ///< Most torque, in newton-meters, spent holding the two bodies at the angle
                              ///< they had when the joint last began to move them: the pose a ragdoll had
                              ///< when it went live. A load needing more turns the joint, so a held limb sags
                              ///< under a weight it cannot carry rather than snapping back. 0 leaves the
                              ///< rotation free.

    glm::quat heldRotation = {1.0f, 0.0f, 0.0f, 0.0f};  ///< The angle held, the connected body's rotation in
                                                        ///< this one's frame; runtime, not serialized.

    bool heldResolved = false;  ///< Whether heldRotation is current; cleared while neither body can move.
                                ///< Runtime.

    glm::vec3 impulse     = {0.0f, 0.0f, 0.0f};  ///< What the anchors held at the end of the last tick, world
                                                 ///< space, the next tick's warm start: a chain solved from
                                                 ///< zero every tick hangs stretched. Runtime, and not rolled
                                                 ///< back by a replay.
    glm::vec3 holdImpulse = {0.0f, 0.0f, 0.0f};  ///< What the hold held, alike

    bool  collideConnected = false;  ///< Whether the two bodies still collide
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::JointType, "Point", "Distance")

VKM_REFLECT_BEGIN(::Vkm::Engine::Joint)
    VKM_F(type, "Point holds the anchors together, Distance a length apart")
    VKM_F(anchor, "On this body, in its frame")
    VKM_F(connectedAnchor, "On the other body, in its frame")
    VKM_F(distance, -1.0f, 1000.0f, "Held apart by this; negative takes it from the first tick")
    VKM_F(stiffness, 0.0f, 1.0f, "How hard drifted anchors are pulled back: 1 at once")
    VKM_F(holdTorque, 0.0f, 10000.0f, "Most torque spent holding their angle; 0 leaves it free")
    VKM_F(collideConnected, "Off: jointed bodies overlap at the joint and would fight")
VKM_REFLECT_END()
