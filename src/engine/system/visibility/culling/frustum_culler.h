#pragma once

#include <glm/glm.hpp>

#include "system/visibility/visibility_context.h"

namespace Engine {

/**
 * @brief Frustum culling: reject AABBs fully outside the view frustum.
 *
 * Uses the half-space test: for each frustum plane, the AABB is outside if its
 * "positive vertex" (furthest along the plane normal) is on the negative side.
 */
namespace FrustumCuller {

/**
 * @brief True if the world AABB is inside or intersects the frustum.
 *
 * Uses center + half-extent formulation: for each plane, computes the signed
 * distance from center to the plane and the projected AABB radius along the
 * plane normal. The AABB is outside if center_distance + radius < 0.
 * This replaces 18 ternary branches (3 per plane) with 6 branchless dot products.
 *
 * @param boundsMin World-space AABB minimum.
 * @param boundsMax World-space AABB maximum.
 * @param context VisibilityContext with frustum.
 */
inline bool isVisible(
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax,
    const VisibilityContext& context
) {
    return Math::frustumIntersectsAABB(context.frustum, boundsMin, boundsMax);
}

} // namespace FrustumCuller

} // namespace Engine
