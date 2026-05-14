#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine::Math {

/**
 * @brief Build a perspective projection matrix.
 * @param fovY   Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param zNear  Near clip plane distance.
 * @param zFar   Far clip plane distance.
 */
inline glm::mat4 makePerspective(float fovY, float aspect, float zNear, float zFar) {
    return glm::perspective(fovY, aspect, zNear, zFar);
}

/**
 * @brief Build an orthographic projection matrix from a half-height and aspect ratio.
 *        Frustum is centred on the origin (no off-axis offset).
 * @param halfHeight Half of the frustum height in world units.
 * @param aspect     Aspect ratio (width / height).
 * @param zNear      Near clip plane distance.
 * @param zFar       Far clip plane distance.
 */
inline glm::mat4 makeOrthographic(float halfHeight, float aspect, float zNear, float zFar) {
    const float halfWidth = halfHeight * aspect;
    return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, zNear, zFar);
}

} // namespace Engine::Math
