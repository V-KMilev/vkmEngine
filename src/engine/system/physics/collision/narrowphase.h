#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "system/physics/collision/contact.h"

namespace Engine {

/**
 * @brief Generate contact points between two oriented boxes in world space.
 *
 * Colliders are sets of boxes, so the narrowphase runs this once per child-box pair.
 * Writes up to MAX_CONTACTS_PER_MANIFOLD entries into @p out and returns the
 * count; 0 means no overlap. Every emitted normal points from box A toward box
 * B, so the caller pushes A along -normal and B along +normal.
 */
int contactBoxes(
    const glm::vec3& centerA, const glm::quat& rotA, const glm::vec3& halfA,
    const glm::vec3& centerB, const glm::quat& rotB, const glm::vec3& halfB,
    Contact* out
);

} // namespace Engine
