#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "ecs/component/render/camera.h"

namespace Vkm::Engine {

/**
 * @brief A view an authoring host renders the frame through instead of the scene's active camera.
 *
 * Set on FrameContext::hostView before the Visibility stage. Either a scene
 * camera named by `through`, active or not, or when it is empty the free view
 * the remaining fields describe, which the scene file does not store.
 */
struct HostView {
    EntityId  through{};                               ///< Scene camera to look through, or empty.
    Camera    camera;                                  ///< Free view's projection; `active` unread.
    glm::vec3 position = glm::vec3(0.0f);              ///< Free view, world space.
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  ///< Free view, world space.
};

} // namespace Vkm::Engine
