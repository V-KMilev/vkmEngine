#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Vkm::Engine {

/**
 * @brief A bone's local position, rotation and scale: the TRS a Transform holds, as an asset keeps it.
 *
 * Laid out as Transform is, so a cooked rig's bind pose is the same bytes (Transform::fromPose).
 */
struct BonePose {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};        ///< Local position
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};  ///< Local rotation
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};        ///< Local scale
};

} // namespace Vkm::Engine
