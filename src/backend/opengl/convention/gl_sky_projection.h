#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "ecs/component/render/camera.h"
#include "system/render/data/camera_data.h"

namespace Vkm::Engine {

/**
 * @brief The projection the sky is drawn through.
 *
 * An orthographic camera's parallel rays would see one direction of sky across the whole
 * view, so it takes a default camera's field of view at its own aspect instead.
 *
 * @param camera The view's camera.
 * @return @p camera's projection when it is perspective, else a default camera's at its aspect.
 */
inline glm::mat4 skyProjection(const CameraData& camera) {
    const glm::mat4& projection = camera.projection;
    if (projection[3][3] == 0.0f) return projection;
    const float aspect = projection[1][1] / projection[0][0];
    return glm::perspective(Camera{}.fovY, aspect, camera.zNear, camera.zFar);
}

} // namespace Vkm::Engine
