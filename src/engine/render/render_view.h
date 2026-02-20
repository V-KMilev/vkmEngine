#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "resource/mesh_asset.h"
#include "resource/material_asset.h"
#include "ecs/component/light.h"

namespace Engine {
    class Scene;
    class ResourceManager;

    struct Visibility;
}

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
 * @brief Representation of a light in the render world.
 *
 * Contains light properties and its transform (position and direction).
 * This is a snapshot of the Light component + Transform at render time.
 */
struct LightData {
    LightType type;
    glm::vec3 color;
    float intensity;
    float radius;
    float innerConeAngle;
    float outerConeAngle;
    bool castShadows;

    glm::vec3 position;
    glm::quat rotation;
};

/**
 * @brief Collection of scene data needed for a rendering pass.
 *
 * Encapsulates camera info, all visible drawables, and active lights required for rendering.
 */
struct RenderView {
    CameraData camera;

    std::vector<DrawableData> drawables;
    std::vector<LightData> lights;

    uint32_t viewportWidth  = 0;
    uint32_t viewportHeight = 0;

    /**
    * @brief Populate this RenderView for the current frame from the scene.
    *
    * Clears and refills internal vectors, reusing existing capacity to avoid
    * per-frame heap allocations. Gathers camera, visible drawables, and lights.
    *
    * @param scene The scene containing all entities and their components.
    * @param resources The resource manager for meshes and materials.
    * @param visibility The visibility listing entities that should be rendered.
    */
    void build(
        const Scene& scene,
        const ResourceManager& resources,
        const Visibility& visibility,
        uint32_t viewportWidth,
        uint32_t viewportHeight
    );

};

} // namespace Engine
