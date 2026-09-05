#pragma once

#include "core/math/bounds.h"

#include <glm/glm.hpp>

#include "system/visibility/visibility_context.h"

namespace Vkm::Engine {

/**
 * @brief Distance culling: reject AABBs whose center is farther than maxDistance from the camera.
 *
 * If context.maxDistance <= 0, culling is disabled (returns true).
 */
namespace DistanceCuller {

/**
 * @brief True if the AABB centre is within maxDistance of the camera.
 *
 * Always true when context.maxDistance is 0 or below, which is how distance
 * culling is switched off.
 *
 * @param bounds World-space AABB.
 * @param context VisibilityContext with cameraPosition and maxDistance.
 */
inline bool isVisible(
    const Math::AABB& bounds,
    const VisibilityContext& context
) {
    if (context.maxDistance <= 0.0f) {
        return true;
    }

    const glm::vec3 worldCenter = bounds.center();
    const glm::vec3 delta = worldCenter - context.cameraPosition;
    const float distanceSquared = glm::dot(delta, delta);

    return distanceSquared <= context.maxDistanceSquared;
}

} // namespace DistanceCuller

} // namespace Vkm::Engine
