#pragma once

#include <vector>

#include "ecs/entity.h"
#include "core/frame/camera_data.h"
#include "core/frame/render_objects.h"

namespace Vkm::Engine {

/**
 * @brief VisibilitySystem's per-frame product: camera data, every scene mesh, and
 *        which of them the camera sees.
 */
struct Visibility {
    RenderObjects objects;  ///< Every drawable mesh, and which the camera sees.

    std::vector<EntityId> entities;  ///< Per object, its entity, parallel to the columns of `objects`. Kept
                                     ///< beside them because the backend never reads it; RenderView (for
                                     ///< outlines), DebugViewSystem and the editor do.

    CameraData camera;  ///< The resolved view; meaningful while hasCamera.
    bool       hasCamera = false;

    EntityId cameraEntity{};  ///< The scene camera the frame was rendered through. Empty for a host's free
                              ///< view (HostView) or no camera.
};

} // namespace Vkm::Engine
