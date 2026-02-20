#pragma once

#include <glm/glm.hpp>

#include "ecs/component/mesh.h"
#include "visibility/visibility_context.h"

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
 *
 * Uses center + half-extent formulation: for each plane, computes the signed
 * distance from center to the plane and the projected AABB radius along the
 * plane normal. The AABB is outside if center_distance + radius < 0.
 * This replaces 18 ternary branches (3 per plane) with 6 branchless dot products.
 *
 * @param mesh Mesh with boundsMin/boundsMax in world space.
 * @param context VisibilityContext with frustum.
 */
inline bool isVisible(
    const Mesh& mesh,
    const VisibilityContext& context
) {
    const glm::vec3 center     = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
    const glm::vec3 halfExtent = (mesh.boundsMax - mesh.boundsMin) * 0.5f;

    const auto& f = context.frustum;
    for (int i = 0; i < 6; ++i) {
        const float dist   = glm::dot(f.normals[i], center) + f.d[i];
        const float radius = glm::dot(f.absNormals[i], halfExtent);
        if (dist + radius < 0.0f) return false;
    }
    return true;
}

} // namespace FrustumCuller

} // namespace Engine
