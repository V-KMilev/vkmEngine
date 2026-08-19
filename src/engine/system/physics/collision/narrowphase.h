#pragma once

#include <glm/glm.hpp>

#include "system/physics/collision/contact.h"

namespace Vkm::Engine {

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
 * @brief A capsule in world space: a segment swept by a radius.
 *
 * The endpoints are the segment's, not the capsule's - the caps extend radius
 * beyond each. a == b is a sphere, which every routine here handles without a
 * special case. The caller places the segment once per body from the local
 * ColliderPart, so nothing here needs the body's rotation.
 */
struct CapsuleShape {
    glm::vec3 a      = {0.0f, -0.5f, 0.0f};
    glm::vec3 b      = {0.0f,  0.5f, 0.0f};
    float     radius = 0.5f;
};

/**
 * @brief Generate contact points between two oriented boxes in world space.
 *
 * Colliders are sets of parts, so the narrowphase runs this once per box-box pair.
 * Writes up to MAX_CONTACTS_PER_MANIFOLD entries into @p out and returns the
 * count; 0 means no overlap. Every emitted normal points from box A toward box
 * B, so the caller pushes A along -normal and B along +normal.
 */
int contactBoxes(const BoxShape& a, const BoxShape& b, Contact* out);

/**
 * @brief Generate contact points between a capsule and an oriented box.
 *
 * Emits one point at the closest feature, or two when the capsule's segment
 * lies flat against a box face - a lying capsule resting on one point would
 * roll off it forever. Normals point from the capsule toward the box; a caller
 * whose capsule is body B negates them rather than calling with the arguments
 * swapped, because no capsule-vs-box test is symmetric.
 *
 * @param a Capsule, world space.
 * @param b Oriented box, world space.
 * @param out Contact array with room for MAX_CONTACTS_PER_MANIFOLD entries.
 * @return Number of contacts written; 0 means no overlap.
 */
int contactCapsuleBox(const CapsuleShape& a, const BoxShape& b, Contact* out);

/**
 * @brief Generate the contact point between two capsules in world space.
 *
 * One point, at the middle of the two surface points on the closest approach of
 * the segments - which is the whole contact unless the two axes are parallel,
 * and a pair of parallel capsules is not a case the engine's characters make.
 *
 * @param a First capsule, world space.
 * @param b Second capsule, world space.
 * @param out Contact array with room for MAX_CONTACTS_PER_MANIFOLD entries.
 * @return Number of contacts written; 0 means no overlap.
 */
int contactCapsuleCapsule(const CapsuleShape& a, const CapsuleShape& b, Contact* out);

} // namespace Vkm::Engine
