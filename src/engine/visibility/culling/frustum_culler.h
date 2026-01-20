#pragma once

#include <glm/glm.hpp>

#include "mesh.h"
#include "visibility_context.h"

namespace Engine {

/**
 * @brief Frustum culling: reject meshes whose world AABB is fully outside the view frustum.
 *
 * Uses the half-space test: for each frustum plane, the AABB is outside if its
 * "positive vertex" (furthest along the plane normal) is on the negative side.
 */
namespace FrustumCuller {

/**
 * @brief True if the mesh’s cached world AABB is inside or intersects the frustum.
 * @param mesh Mesh with boundsMin/boundsMax in world space.
 * @param context VisibilityContext with frustum.
 */
inline bool isVisible(
    const Mesh& mesh,
    const VisibilityContext& context
) {
    for (const auto& plane : context.frustum.planes) {
        const glm::vec3 normal = glm::vec3(plane);
        const float distance = plane.w;

        glm::vec3 positiveVertex;
        positiveVertex.x = (normal.x >= 0.0f) ? mesh.boundsMax.x : mesh.boundsMin.x;
        positiveVertex.y = (normal.y >= 0.0f) ? mesh.boundsMax.y : mesh.boundsMin.y;
        positiveVertex.z = (normal.z >= 0.0f) ? mesh.boundsMax.z : mesh.boundsMin.z;

        if (glm::dot(normal, positiveVertex) + distance < glm::epsilon<float>()) {
            return false;
        }
    }
    return true;
}

} // namespace FrustumCuller

} // namespace Engine
