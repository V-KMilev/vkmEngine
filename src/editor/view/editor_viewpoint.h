#pragma once

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Where the editor looks at a scene from: a pose of its own, not an entity.
 *
 * Kept per scene in `editor_settings.json`. No roll, so yaw and pitch are its whole orientation.
 */
struct EditorViewpoint {
    glm::vec3 position = glm::vec3(0.0f, 2.0f, 6.0f);  ///< The default, where a scene with no camera is first
                                                       ///< seen from, sits back along +Z facing the origin.
    float     yaw      = 0.0f;                         ///< Radians about world up; see Math::fromYawPitch.
    float     pitch    = 0.0f;                         ///< Radians above the horizon.

    bool  orthographic = false;  ///< Parallel projection, zoomed by orthoHeight.
    float orthoHeight  = 10.0f;  ///< Half the view's height in world units, when orthographic.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::EditorViewpoint)
    VKM_F(position)
    VKM_F(yaw)
    VKM_F(pitch)
    VKM_F(orthographic)
    VKM_F(orthoHeight, 0.001f, 1000000.0f, "Half the view's height in world units")
VKM_REFLECT_END()
