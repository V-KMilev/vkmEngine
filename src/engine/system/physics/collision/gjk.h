#pragma once

#include <glm/glm.hpp>

#include "system/physics/collision/contact.h"
#include "system/physics/collision/support.h"

namespace Vkm::Engine {

/**
 * @brief Whether two convex shapes overlap, by GJK.
 *
 * Searches the Minkowski difference of the two shapes for the origin: the
 * difference contains it exactly when the shapes share a point. Nothing here
 * knows what either shape is - only how to ask each for its extreme point in a
 * direction - so one routine covers every pair of convex shapes rather than one
 * routine per pair.
 *
 * @param a First shape.
 * @param b Second shape.
 * @return True when the two overlap or touch.
 */
bool gjkOverlap(const SupportShape& a, const SupportShape& b);

/**
 * @brief Overlap of two convex shapes, with the contact that resolves it.
 *
 * GJK answers whether, and leaves behind a simplex enclosing the origin; EPA
 * grows that simplex out to the surface of the Minkowski difference to find the
 * nearest face, whose normal and distance are the shallowest direction the two
 * can be pushed apart and by how much.
 *
 * One contact point, not a manifold, which is why this supplements the
 * hand-written primitive routines rather than replacing them: a box resting on
 * a box needs four points to stay still. It is enough for a hull or a mesh
 * triangle, where the alternative is no contact at all.
 *
 * @param a First shape; the normal points away from this one.
 * @param b Second shape.
 * @param[out] out Contact, written only when the two overlap.
 * @return True when the two overlap, and @p out was written.
 */
bool gjkContact(const SupportShape& a, const SupportShape& b, Contact& out);

/**
 * @brief Distance between two convex shapes, and the direction that closes it.
 *
 * The closest-point form of the same search: the simplex walks toward the
 * origin of the Minkowski difference and settles on the feature nearest it,
 * whose length is the gap between the shapes. What a sweep needs and an
 * overlap test cannot give - a single support plane bounds the gap in one
 * direction only, and declaring a hit against it reports contact beside a
 * long shape metres from its surface.
 *
 * @param a First shape.
 * @param b Second shape.
 * @param[out] direction Unit direction from @p a toward @p b along the gap;
 *             untouched when the shapes overlap.
 * @return The separation distance, or zero when the shapes overlap or touch.
 */
float gjkDistance(const SupportShape& a, const SupportShape& b, glm::vec3& direction);

} // namespace Vkm::Engine
