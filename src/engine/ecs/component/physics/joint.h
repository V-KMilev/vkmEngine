#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/reflect.h"
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

    /**
     * @brief Distance the anchors are held at, in metres, for a Distance joint.
     *
     * Negative means "whatever they were when the joint was made", resolved on the first tick.
     */
    float distance = -1.0f;

    /**
     * @brief The length actually held, once the solver has measured it; runtime, not serialized.
     *
     * Keeps `distance` as authored, including a negative that asks for a measurement.
     */
    float resolvedDistance = -1.0f;

    /**
     * @brief How hard the joint pulls drifted anchors back together, 0 to 1.
     *
     * Scales the target the solver converges on, not a spring constant or each pass's impulse, so it
     * cannot be tuned into instability or change meaning with the iteration count. At 1 drift closes
     * overdamped, at the joint spring's full rate; at 0 relative motion is still held but drift is left.
     */
    float stiffness = 1.0f;

    /**
     * @brief Most torque, in newton-metres, spent holding the two bodies at the angle between them.
     *
     * The angle is the one they had when the joint last began to move them: the pose a ragdoll had
     * when it went live. A load that needs more than this turns the joint, so a held limb sags under
     * a weight it cannot carry rather than snapping back. 0 leaves the rotation free.
     */
    float holdTorque = 0.0f;

    /// The angle held, the connected body's rotation in this one's frame; runtime, not serialized.
    glm::quat heldRotation = {1.0f, 0.0f, 0.0f, 0.0f};

    /// Whether heldRotation is current; cleared while neither body can move. Runtime.
    bool heldResolved = false;

    bool  collideConnected = false;   ///< Whether the two bodies still collide
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::JointType, "Point", "Distance")

VKM_REFLECT_BEGIN(::Vkm::Engine::Joint)
    VKM_F(type)
    VKM_F(anchor)
    VKM_F(connectedAnchor)
    VKM_F(distance)
    VKM_F(stiffness)
    VKM_F(holdTorque)
    VKM_F(collideConnected)
VKM_REFLECT_END()
