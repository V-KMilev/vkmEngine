#pragma once

#include <glm/glm.hpp>

#include "system/visibility/visibility_context.h"

namespace Vkm::Engine {

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
 * Thin wrapper over Math::frustumIntersectsAABB (core/math/frustum.h), which
 * holds the actual center + half-extent half-space test.
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

} // namespace Vkm::Engine
