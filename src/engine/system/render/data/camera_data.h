#pragma once

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Flattened camera for the frame.
 *
 * Filled once from the active camera so the backend never searches the scene.
 */
struct CameraData {
    glm::mat4 view;           ///< The view matrix for the camera.
    glm::mat4 projection;     ///< The projection matrix for the camera.
    glm::vec3 position;       ///< The position of the camera in world space.
};

} // namespace Engine
