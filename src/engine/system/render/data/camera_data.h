#pragma once

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Flattened camera for the frame.
 *
 * Filled once from the active camera so the backend never searches the scene.
 */
struct CameraData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 position;
};

} // namespace Engine
