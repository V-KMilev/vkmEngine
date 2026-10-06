#pragma once

#include <glm/glm.hpp>

#include "system/physics/collision/contact.h"

namespace Vkm::Engine {

struct SupportShape;

/**
 * @brief An oriented box in world space: centre, three unit axes, half extents.
 *
 * The form the separating-axis test indexes. A caller casts its quaternion to axes once per body, not
 * once per box pair.
 */
struct BoxShape {
    glm::vec3 center      = {0.0f, 0.0f, 0.0f};
    glm::vec3 axes[3]     = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};
};

/**
 * @brief A capsule in world space: a segment swept by a radius.
 *
 * The endpoints are the segment's; the caps extend radius beyond each. a == b is a sphere, handled
 * without a special case.
 */
struct CapsuleShape {
    glm::vec3 a      = {0.0f, -0.5f, 0.0f};
    glm::vec3 b      = {0.0f,  0.5f, 0.0f};
    float     radius = 0.5f;
};

/**
 * @brief Generate contact points between two oriented boxes in world space.
 *
 * Normals point from box A toward box B.
 *
 * @param a First box, world space.
 * @param b Second box, world space.
 * @param out Room for MAX_CONTACTS_PER_MANIFOLD entries.
 * @return Contacts written; 0 means no overlap.
 */
int contactBoxes(const BoxShape& a, const BoxShape& b, Contact* out);

/**
 * @brief Generate contact points between a capsule and an oriented box.
 *
 * One point at the closest feature, or two when the segment lies flat on a face, since a lying capsule
 * on one point would roll forever. Normals point from capsule to box; a caller whose capsule is body B
 * negates them, as the test is not symmetric.
 *
 * @param a Capsule, world space.
 * @param b Oriented box, world space.
 * @param out Room for MAX_CONTACTS_PER_MANIFOLD entries.
 * @return Contacts written; 0 means no overlap.
 */
int contactCapsuleBox(const CapsuleShape& a, const BoxShape& b, Contact* out);

/**
 * @brief Generate the contact point between two capsules in world space.
 *
 * One point, midway between the surface points at the segments' closest approach. For parallel axes
 * the overlap is a line and one point stands for it: enough to hold upright, rotation-frozen characters
 * apart, and a pivot for two lying capsules.
 *
 * @param a First capsule, world space.
 * @param b Second capsule, world space.
 * @param out Room for MAX_CONTACTS_PER_MANIFOLD entries.
 * @return Contacts written; 0 means no overlap.
 */
int contactCapsuleCapsule(const CapsuleShape& a, const CapsuleShape& b, Contact* out);

/**
 * @brief Generate the contacts between one triangle and any convex shape.
 *
 * GJK decides overlap; the manifold is the shape's feature facing the triangle (a box's face, a flat
 * capsule's segment, else its deepest point) clipped to the triangle's edges and kept below its plane,
 * or the deepest point below the plane when the feature misses the interior.
 *
 * A shape reaching in past some edge less than below the face meets the triangle from the side and gets
 * nothing: a neighbouring face (a ledge's riser) holds it.
 *
 * The winding decides the front, and depth is along the face normal: a zero-thickness hull is symmetric,
 * so a body sunk through a floor would otherwise be pushed on down. Normals point from triangle to
 * shape; a caller whose mesh is body B negates them.
 *
 * @param triangle Three corners, world space, wound counter-clockwise about the front.
 * @param shape The convex shape, world space.
 * @param out Room for MAX_CONTACTS_PER_MANIFOLD entries.
 * @return Contacts written; 0 means no overlap, or a triangle with no face to speak of.
 */
int contactTriangle(const glm::vec3* triangle, const SupportShape& shape, Contact* out);

} // namespace Vkm::Engine
