#pragma once

#include <vector>

#include "ecs/entity.h"
#include "system/render/data/camera_data.h"
#include "system/render/data/render_objects.h"

namespace Vkm::Engine {

/**
 * @brief VisibilitySystem's per-frame product: camera data, every scene mesh, and
 *        which of them the camera sees.
 */
struct Visibility {
    RenderObjects objects;  ///< Every drawable mesh, and which the camera sees.

    /**
     * @brief Per object, its entity, parallel to the columns of `objects`.
     *
     * Kept beside the objects because drawing never needs it; tools do.
     */
    std::vector<EntityId> entities;

    CameraData camera;  ///< The resolved view, depth of field included; meaningful while hasCamera.
    bool       hasCamera = false;

    /**
     * @brief The scene camera the frame was rendered through.
     *
     * Empty for a host's free view (HostView) or no camera.
     */
    EntityId cameraEntity{};
};

} // namespace Vkm::Engine
