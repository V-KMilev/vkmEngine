#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "resource_handle.h"

namespace Engine {

/**
 * @brief Camera data used for rendering calculations.
 *
 * Stores matrices needed for transforming world coordinates to camera/view space,
 * as well as the world-space position of the camera.
 */
struct CameraData {
    glm::mat4 view           = {1.0f};
    glm::mat4 projection     = {1.0f};
    glm::mat4 viewProjection = {1.0f};

    glm::vec3 position = {0.0f, 0.0f, 0.0f};
};

/**
 * @brief Representation of a single drawable object within the render world.
 *
 * Associates mesh and material resources with a model matrix.
 */
struct DrawableData {
    MeshHandle mesh;
    MaterialHandle material;

    glm::mat4 model = {1.0f};
};

/**
 * @brief Collection of scene data needed for a rendering pass.
 *
 * Encapsulates camera info, all visible drawables, and active lights (soon) required for rendering.
 */
struct RenderView {
    CameraData camera;

    std::vector<DrawableData> drawables;
};

} // namespace Engine
