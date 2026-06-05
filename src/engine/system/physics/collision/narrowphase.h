#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/component/collider.h"
#include "system/physics/collision/contact.h"

namespace Engine {

/**
 * @brief Generate contact points between two colliders in their world poses.
 *
 * Dispatches on the shape pair (sphere/box/plane in any order). Writes up to
 * MAX_CONTACTS_PER_MANIFOLD entries into out and returns the count; 0 means no
 * overlap. Every emitted normal points from body A toward body B, so the caller
 * pushes A along -normal and B along +normal.
 */
int generateContacts(
    const Collider& a, const glm::vec3& posA, const glm::quat& rotA,
    const Collider& b, const glm::vec3& posB, const glm::quat& rotB,
    Contact* out
);

} // namespace Engine
