#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>

#include "core/math/bounds.h"

namespace Vkm::Engine::Math {

/**
 * @brief View frustum for culling: six planes from a view-projection matrix.
 *
 * Stores abs(normal) so the AABB test is branchless dot products.
 * Order: left, right, bottom, top, near, far.
 */
struct Frustum {
    glm::vec3 normals[6];     ///< Normalized.
    glm::vec3 absNormals[6];
    float     d[6];           ///< The constant in ax+by+cz+d=0.
};

/**
 * @brief Extract six normalized frustum planes from a view-projection matrix.
 *
 * @param viewProjection Projection * view, world to clip space.
 * @return Planes in order: left, right, bottom, top, near, far.
 */
inline Frustum extractFrustum(const glm::mat4& viewProjection) {
    Frustum frustum;
    const glm::vec4 row0 = glm::row(viewProjection, 0);
    const glm::vec4 row1 = glm::row(viewProjection, 1);
    const glm::vec4 row2 = glm::row(viewProjection, 2);
    const glm::vec4 row3 = glm::row(viewProjection, 3);
    glm::vec4 planes[6] = {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row3 + row2, row3 - row2};
    for (int i = 0; i < 6; ++i) {
        const glm::vec3 n = glm::vec3(planes[i]);
        const float length = glm::length(n);
        if (length > 0.0f) {
            const float invLen = 1.0f / length;
            frustum.normals[i] = n * invLen;
            frustum.d[i]       = planes[i].w * invLen;
        } else {
            frustum.normals[i] = glm::vec3(0.0f);
            frustum.d[i]       = 0.0f;
        }
        frustum.absNormals[i] = glm::abs(frustum.normals[i]);
    }
    return frustum;
}

/**
 * @brief True if the world-space AABB is inside or intersects the frustum.
 *
 * @param f Frustum to test against.
 * @param bounds Box in world space.
 * @return False only when the box lies wholly outside one plane.
 */
inline bool frustumIntersectsAABB(const Frustum& f, const AABB& bounds) {
    const glm::vec3 center     = bounds.center();
    const glm::vec3 halfExtent = bounds.halfExtent();
    for (int i = 0; i < 6; ++i) {
        const float dist   = glm::dot(f.normals[i], center) + f.d[i];
        const float radius = glm::dot(f.absNormals[i], halfExtent);
        if (dist + radius < 0.0f) return false;
    }
    return true;
}

} // namespace Vkm::Engine::Math
