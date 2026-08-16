#pragma once

#include <glm/glm.hpp>

#include "system/physics/collision/contact.h"

namespace Engine {

/**
 * @brief An oriented box in world space: centre, three unit axes, half extents.
 *
 * The form the separating-axis test indexes directly. A caller holding a
 * rotation as a quaternion casts it to a matrix once per body and places every
 * child box against those axes, rather than once per box pair here.
 */
struct BoxShape {
    glm::vec3 center      = {0.0f, 0.0f, 0.0f};
    glm::vec3 axes[3]     = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};
};

/**
 * @brief Generate contact points between two oriented boxes in world space.
 *
 * Colliders are sets of boxes, so the narrowphase runs this once per child-box pair.
 * Writes up to MAX_CONTACTS_PER_MANIFOLD entries into @p out and returns the
 * count; 0 means no overlap. Every emitted normal points from box A toward box
 * B, so the caller pushes A along -normal and B along +normal.
 */
int contactBoxes(const BoxShape& a, const BoxShape& b, Contact* out);

} // namespace Engine
