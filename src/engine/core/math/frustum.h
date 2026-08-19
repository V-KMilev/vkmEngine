#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>

namespace Vkm::Engine::Math {

/**
 * @brief View frustum for culling: six planes from a view-projection matrix.
 *
 * Planes are normalized and store pre-computed abs(normal) so the AABB test is
 * a handful of branchless dot products instead of per-corner selects.
 * Order: left, right, bottom, top, near, far.
 */
struct Frustum {
    glm::vec3 normals[6];     ///< Plane normals (a,b,c), normalized.
    glm::vec3 absNormals[6];  ///< abs(normal) per plane, pre-computed once.
    float     d[6];           ///< Plane distance d (the constant in ax+by+cz+d=0).
};

/**
 * @brief Extract six normalized frustum planes from a view-projection matrix.
 *
 * @param viewProjection Combined projection * view (the matrix that maps world
 *        space to clip space).
 * @return Frustum with planes in order: left, right, bottom, top, near, far.
 */
inline Frustum extractFrustum(const glm::mat4& viewProjection) {
    Frustum frustum;
    const glm::vec4 row0 = glm::row(viewProjection, 0);
    const glm::vec4 row1 = glm::row(viewProjection, 1);
    const glm::vec4 row2 = glm::row(viewProjection, 2);
    const glm::vec4 row3 = glm::row(viewProjection, 3);
    glm::vec4 planes[6] = {
        row3 + row0, row3 - row0,
        row3 + row1, row3 - row1,
        row3 + row2, row3 - row2
    };
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
 * Center + half-extent half-space test: for each plane, the box is outside when
 * the signed center distance plus the projected AABB radius is negative.
 *
 * @param boundsMin World-space AABB minimum.
 * @param boundsMax World-space AABB maximum.
 */
inline bool frustumIntersectsAABB(
    const Frustum& f,
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax
) {
    const glm::vec3 center     = (boundsMin + boundsMax) * 0.5f;
    const glm::vec3 halfExtent = (boundsMax - boundsMin) * 0.5f;
    for (int i = 0; i < 6; ++i) {
        const float dist   = glm::dot(f.normals[i], center) + f.d[i];
        const float radius = glm::dot(f.absNormals[i], halfExtent);
        if (dist + radius < 0.0f) return false;
    }
    return true;
}

} // namespace Vkm::Engine::Math
