#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>

namespace Engine {

/**
 * @brief View frustum for culling: six planes from the view-projection matrix.
 *
 * Each plane is ax + by + cz + d = 0; (a,b,c) is the normal. Order: left, right,
 * bottom, top, near, far.
 */
struct Frustum {
    glm::vec4 planes[6];
};

/**
 * @brief Per-frame data for visibility and culling (frustum, matrices, viewport, thresholds).
 */
struct VisibilityContext {
    Frustum frustum;             ///< View frustum planes (from extractFrustum).
    glm::vec3 cameraPosition;    ///< Camera position in world space.
    glm::mat4 view;              ///< View matrix (world → view space).
    glm::mat4 projection;        ///< Projection matrix (view → clip space).

    uint32_t viewportWidth;      ///< Viewport width in pixels.
    uint32_t viewportHeight;     ///< Viewport height in pixels.

    float minPixels;             ///< Min projected size in pixels; below this, screen-size culling rejects. <= 0 disables.
    float maxDistance;           ///< Max distance from camera; beyond this, distance culling rejects. <= 0 disables.
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
    frustum.planes[0] = row3 + row0;
    frustum.planes[1] = row3 - row0;
    frustum.planes[2] = row3 + row1;
    frustum.planes[3] = row3 - row1;
    frustum.planes[4] = row3 + row2;
    frustum.planes[5] = row3 - row2;
    for (auto& plane : frustum.planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 0.0f) plane /= length;
    }
    return frustum;
}

} // namespace Engine