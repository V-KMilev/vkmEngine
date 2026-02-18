#pragma once

#include <glm/glm.hpp>

#include "ecs/component/mesh.h"
#include "visibility/visibility_context.h"

namespace Engine {

/**
 * @brief Distance culling: reject meshes whose AABB center is farther than maxDistance from the camera.
 *
 * If context.maxDistance <= 0, culling is disabled (returns true).
 */
namespace DistanceCuller {

/**
 * @brief True if the mesh’s AABB center is within maxDistance of the camera (or maxDistance <= 0).
 * @param mesh Mesh with world boundsMin/boundsMax.
 * @param context VisibilityContext with cameraPosition and maxDistance.
 */
inline bool isVisible(
    const Mesh& mesh,
    const VisibilityContext& context
) {
    if (context.maxDistance <= 0.0f) {
        return true;
    }

    const glm::vec3 worldCenter = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
    const glm::vec3 delta = worldCenter - context.cameraPosition;
    const float distanceSquared = glm::dot(delta, delta);

    return distanceSquared <= (context.maxDistance * context.maxDistance);
}

} // namespace DistanceCuller

} // namespace Engine
