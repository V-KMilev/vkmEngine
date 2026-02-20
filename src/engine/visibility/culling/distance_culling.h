#pragma once

#include <glm/glm.hpp>

#include "visibility/visibility_context.h"

namespace Engine {

/**
 * @brief Distance culling: reject AABBs whose center is farther than maxDistance from the camera.
 *
 * If context.maxDistance <= 0, culling is disabled (returns true).
 */
namespace DistanceCuller {

/**
 * @brief True if the AABB center is within maxDistance of the camera (or maxDistance <= 0).
 * @param boundsMin World-space AABB minimum.
 * @param boundsMax World-space AABB maximum.
 * @param context VisibilityContext with cameraPosition and maxDistance.
 */
inline bool isVisible(
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax,
    const VisibilityContext& context
) {
    if (context.maxDistance <= 0.0f) {
        return true;
    }

    const glm::vec3 worldCenter = (boundsMin + boundsMax) * 0.5f;
    const glm::vec3 delta = worldCenter - context.cameraPosition;
    const float distanceSquared = glm::dot(delta, delta);

    return distanceSquared <= context.maxDistanceSquared;
}

} // namespace DistanceCuller

} // namespace Engine
