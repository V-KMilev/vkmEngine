#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>

#include "system/visibility/culling/occlusion_oracle.h"

namespace Engine {

/**
 * @brief View frustum for culling: six planes from the view-projection matrix.
 *
 * Pre-extracted normals and abs-normals avoid vec4->vec3 conversions and
 * per-entity glm::abs() calls in the hot culling loop.
 * Order: left, right, bottom, top, near, far.
 */
struct Frustum {
    glm::vec3 normals[6];     ///< Plane normals (a,b,c), pre-extracted from vec4.
    glm::vec3 absNormals[6];  ///< abs(normal) per plane, pre-computed once.
    float     d[6];           ///< Plane distance (the w component of ax+by+cz+d=0).
};

/**
 * @brief Per-frame data for visibility and culling (frustum, matrices, viewport, thresholds).
 */
struct VisibilityContext {
    Frustum frustum;             ///< View frustum planes (from extractFrustum).
    glm::vec3 cameraPosition;    ///< Camera position in world space.
    glm::mat4 view;              ///< View matrix (world -> view space).
    glm::mat4 projection;        ///< Projection matrix (view -> clip space).

    uint32_t viewportWidth;      ///< Viewport width in pixels.
    uint32_t viewportHeight;     ///< Viewport height in pixels.

    float minPixels;             ///< Min projected size in pixels; below this, screen-size culling rejects. <= 0 disables.
    float maxDistance;           ///< Max distance from camera; beyond this, distance culling rejects. <= 0 disables.
    float maxDistanceSquared;    ///< Pre-computed maxDistance^2 for squared-distance comparisons.
    float screenSizeThresholdSq; ///< Pre-computed (minPixels / (projScaleY * viewportHeight))^2 for sqrt-free screen-size test.

    /// One-frame-stale Hi-Z occlusion pyramid, snapshotted ONCE per frame by
    /// VisibilitySystem and shared read-only across every cull worker. Null
    /// when occlusion is inactive. Replaces the previous per-entity
    /// OcclusionOracle::snapshot() that deep-copied the whole pyramid under a
    /// mutex for every AABB tested (CODE_REVIEW.md #24).
    const OcclusionOracle::Frame* occlusion = nullptr;
};

/**
 * @brief Extract six normalized frustum planes from a view-projection matrix.
 * @param viewProjection Combined view * projection (or projection * view depending on convention).
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
            frustum.normals[i]    = n * invLen;
            frustum.d[i]          = planes[i].w * invLen;
        } else {
            frustum.normals[i]    = glm::vec3(0.0f);
            frustum.d[i]          = 0.0f;
        }
        frustum.absNormals[i] = glm::abs(frustum.normals[i]);
    }
    return frustum;
}

} // namespace Engine