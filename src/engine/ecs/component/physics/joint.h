#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief What a joint holds fixed between two bodies.
 *
 * Serialized by name, so reordering these without renaming keeps scenes valid.
 */
enum class JointType : uint8_t {
    Point    = 0,  ///< Anchors coincide; rotation is free. A ball and socket.
    Distance = 1,  ///< Anchors stay a set distance apart. A rod: it pushes as well as pulls.
    Count          ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief Ties this entity's body to another, at a point on each.
 *
 * A constraint rather than a force: the solver removes the relative motion the
 * joint forbids in the same passes it resolves contacts, so a jointed body
 * stacks, sleeps and comes to rest like any other. Springs would fight the
 * contact solver for authority over the same velocities and lose visibly.
 *
 * The anchors are in each body's own frame, which is what makes a joint
 * survive both bodies moving: a world-space anchor would have to be rewritten
 * every time either one did.
 *
 * `connected` may be an entity with no Rigidbody, in which case the joint holds
 * the body to a fixed point in the world - that is what pins a rope's top end
 * or hangs a sign from a wall.
 */
struct Joint {
    JointType type = JointType::Point;

    EntityId  connected{};                    ///< The other body; empty disables
    glm::vec3 anchor = {0.0f, 0.0f, 0.0f};    ///< On this body, local
    glm::vec3 connectedAnchor = {0.0f, 0.0f, 0.0f};  ///< On the other, local

    /**
     * @brief Distance the anchors are held at, in metres. Distance joints only.
     *
     * Negative means "whatever they were when the joint was made", resolved on
     * the first tick. An authored rope has a length someone chose; a rope built
     * at play time has whatever length it was built with, and asking the author
     * to compute it is asking them to do the solver's arithmetic.
     */
    float distance = -1.0f;

    /**
     * @brief The length actually being held, once the solver has measured it.
     *
     * Runtime, and not serialized: `distance` is what the author wrote and must
     * survive a save unchanged, including the negative that asks for a
     * measurement. The measurement lands here, so the request is never spent.
     */
    float resolvedDistance = -1.0f;

    /**
     * @brief How fast the joint closes the gap between its anchors, 0 to 1.
     *
     * A fraction of the remaining error removed per tick rather than a spring
     * constant: it cannot be tuned into instability, and 1 is a rigid joint
     * rather than an explosive one.
     *
     * It scales the target the solver converges on, not each pass's impulse,
     * so what it means does not change with the scene's solver iteration
     * count. At 0 the anchors are still held rigidly against relative motion -
     * that is what makes it a joint - but drift already there is left alone.
     */
    float stiffness = 1.0f;

    bool  collideConnected = false;   ///< Whether the two bodies still collide
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::JointType, "Point", "Distance")

VKM_REFLECT_BEGIN(::Vkm::Engine::Joint)
    VKM_F(anchor),
    VKM_F(connectedAnchor),
    VKM_F(distance),
    VKM_F(stiffness),
    VKM_F(collideConnected)
VKM_REFLECT_END()
