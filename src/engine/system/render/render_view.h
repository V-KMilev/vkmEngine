#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "resource/mesh_asset.h"
#include "resource/material_asset.h"
#include "ecs/component/light.h"

namespace Engine {

class Scene;
class ResourceManager;
struct Visibility;

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
    float     exposure = 1.0f;
};

/**
 * @brief Representation of a single drawable object within the render world.
 *
 * Associates mesh and material resources with a model matrix.
 */
struct DrawableData {
    MeshHandle mesh;
    MaterialHandle material;
    MaterialType materialType = MaterialType::Opaque;
    bool castShadows = true;

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

    bool  castShadows;
    float shadowBias;
    float shadowExtent;
    int   shadowSlot;     ///< Assigned by RenderView::build. -1 = no shadow. Used as atlas layer / cube index.

    glm::vec3 position;
    glm::quat rotation;
};

/**
 * @brief Backend-agnostic environment/scene settings.
 *
 * Written by the editor, read by backend passes during rendering.
 * Lives on RenderSystem, copied into RenderView each frame.
 */
struct EnvironmentConfig {
    // Ambient light (fallback when no environment map is set)
    glm::vec3 ambientColor     = glm::vec3(1.0f);
    float     ambientIntensity = 0.03f;

    // Image-based lighting. Empty path = no IBL (uses the flat ambient above).
    // The bake pass (re)bakes whenever this path changes.
    std::string environmentMapPath = "";
    float       iblIntensity       = 1.0f;

    // Screen-space ambient occlusion (GTAO), modulates the ambient term.
    bool  ssao          = true;
    float ssaoRadius    = 0.5f;
    float ssaoIntensity = 0.8f;   // full-strength GTAO tends to over-darken contact

    // Screen-space reflections, additively blended into the HDR scene.
    bool  ssr            = true;
    float ssrIntensity   = 0.6f;
    float ssrMaxDistance = 8.0f;   // view-space ray length
    float ssrThickness   = 0.5f;   // view-space hit tolerance

    // Temporal anti-aliasing (camera-reprojection). Off by default; MSAA
    // already does spatial edge AA, so this is temporal stabilisation.
    bool  taa      = false;
    float taaBlend = 0.5f;         // history weight (only used when TAA on)

    // Depth of field (off by default). View-space focus.
    bool  dof              = false;
    float dofFocusDistance = 5.0f;
    float dofFocusRange    = 50.0f;
    float dofMaxBlur       = 0.001f;  // gather radius in UV

    // Camera motion blur (off by default).
    bool  motionBlur         = false;
    float motionBlurStrength = 0.5f;

    // Color-grading LUT (16^3 strip), applied post-display. Empty path /
    // disabled = identity (no regression).
    bool        colorGrade          = false;
    std::string colorLutPath        = "";
    float       colorGradeIntensity = 1.0f;

    // Display transform / tone mapping curve owned by the composite pass.
    // 0 = AgX (filmic, desaturates highlights), 1 = Khronos PBR Neutral
    // (albedo-faithful - matches online glTF viewers), 2 = ACES, 3 = Reinhard.
    // Default 1: makes metals/colored materials read like the PBR references.
    int tonemap = 1;

    // Post-processing. Bloom is blended in linear HDR before exposure.
    float bloomStrength = 0.01f;

    // Auto-exposure (eye adaptation). OFF by default: fixed-exposure is what
    // glTF/PBR reference viewers (Khronos sample-viewer, model-viewer) use, so
    // material color/contrast reads as intended instead of being flattened by
    // continuous re-metering. When on, the composite derives exposure from the
    // adapted scene luminance and the camera exposure becomes EV bias.
    bool  autoExposure  = false;
    float exposureKey   = 0.18f;   // target middle-gray
    float exposureSpeed = 2.5f;    // adaptation rate (per second; 1.5 felt sluggish)
    float exposureMin   = 0.03f;   // clamp on the derived exposure
    float exposureMax   = 8.0f;

    // Background
    glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.12f, 1.0f);

    // Grid
    bool  gridEnabled   = true;
    float gridSize      = 1000.0f;
    float gridScale     = 1.0f;
    float gridFadeStart = 50.0f;
    float gridFadeEnd   = 550.0f;

    // Debug visualization (AABB wireframes). Off by default.
    bool      aabbDebug  = false;
    glm::vec3 debugColor = glm::vec3(1.0f, 0.0f, 0.0f);

    // Rendering
    bool wireframe = false;
};

/**
 * @brief Collection of scene data needed for a rendering pass.
 *
 * Encapsulates camera info, all visible drawables, and active lights required for rendering.
 */
struct RenderView {
    CameraData camera;
    EnvironmentConfig environment;

    std::vector<DrawableData> drawables;

    /// Shadow-casting geometry for the shadow pass. Built from the WHOLE
    /// scene (not the camera frustum) so occluders behind / beside the
    /// camera still cast shadows into view - camera-frustum culling here is
    /// what made shadows pop and flicker as the view moved.
    std::vector<DrawableData> shadowCasters;

    std::vector<LightData> lights;

    uint32_t viewportWidth  = 0;
    uint32_t viewportHeight = 0;

    float deltaTime = 0.0f;  ///< Real seconds since last frame (eye adaptation)

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
