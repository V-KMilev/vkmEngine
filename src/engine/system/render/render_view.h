#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "resource/mesh_asset.h"
#include "resource/material_asset.h"
#include "ecs/component/light.h"
#include "ecs/entity.h"
#include "system/render/environment_config.h"  // EnvironmentConfig, sub-configs, RenderMode

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

    /// Near/far clip planes copied from the Camera component each frame.
    /// Used by the PBR shader's Depth diagnostic to linearise depth into
    /// [0, 1] across the camera's actual range instead of a hardcoded one.
    float     zNear    = 0.1f;
    float     zFar     = 1000.0f;
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

    /// World-space AABB precomputed by VisibilitySystem - cached so debug
    /// passes (AABB visualization, picking) don't re-run localToWorldAABB.
    glm::vec3 worldMin = {0.0f, 0.0f, 0.0f};
    glm::vec3 worldMax = {0.0f, 0.0f, 0.0f};

    /// True when the source entity carries the `Selected` tag component.
    /// Drives the outline pass; ignored by every other pass.
    bool selected = false;
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

    // Area-light geometry. Width/height for Rect, radius for Disk. twoSided
    // controls whether the back face emits. Ignored for Directional/Point/Spot.
    float areaWidth  = 1.0f;
    float areaHeight = 1.0f;
    float areaRadius = 0.5f;
    bool  twoSided   = false;

    bool  castShadows;
    float shadowBias;
    float shadowExtent;
    int   shadowSlot;     ///< Assigned by RenderView::build. -1 = no shadow. Used as atlas layer / cube index.

    glm::vec3 position;
    glm::quat rotation;
};

/**
 * @brief Persistent cache of the sorted shadow-caster set.
 *
 * The shadow casters are every visible, shadow-casting, opaque mesh in the
 * scene, sorted by (material.id, mesh.id) - a key that does NOT depend on the
 * world matrix. So the SET and its sort order change only on *structural*
 * edits (entity/Mesh add/remove, Mesh.{visible,castShadows,mesh,material}
 * edits, MaterialAsset.type flips, scene load) - never on movement. This cache
 * keeps the sorted caster identities across frames; RenderView::build rebuilds
 * them only when a cheap per-frame structural fingerprint changes, and
 * refreshes their matrices live every frame, so transform changes (animation,
 * gizmo, hierarchy) need no invalidation.
 *
 * MUST live on RenderSystem (single instance), NOT inside RenderView - the
 * views are double-buffered, so a cache there would alternate buffers and
 * never hit. Touched only on the main thread in buildView(), so no locking.
 */
struct ShadowCasterCache {
    struct Entry {
        EntityId       entity;    ///< Source entity (for the live matrix refresh).
        MeshHandle     mesh;
        MaterialHandle material;
    };
    std::vector<Entry> sorted;          ///< Caster identities, sorted by (material.id, mesh.id).
    uint64_t fingerprint   = 0;         ///< Hash over the caster set's identity + sort-key fields.
    uint32_t count         = 0;         ///< Exact caster count (guards the hash against add/remove collisions).
    uint64_t globalVersion = ~0ull;     ///< ResourceManager global version the cache was built against (scene-load guard).
    bool     valid         = false;     ///< False until first build / after a forced reset.
};

/**
 * @brief Collection of scene data needed for a rendering pass.
 *
 * Encapsulates camera info, all visible drawables, and active lights required for rendering.
 */
struct RenderView {
    CameraData camera;
    EnvironmentConfig environment;

    /**
     * @brief Pre-resolved facts for the current frame's render mode.
     *
     * Cheap to derive (a switch on env.renderMode); passes read this instead
     * of branching on the enum directly so a new mode is one place to update
     * (resolveModeConfig).
     */
    RenderModeConfig modeConfig;

    std::vector<DrawableData> drawables;

    /**
     * @brief Shadow-casting geometry for the shadow pass.
     *
     * Built from the WHOLE scene (not the camera frustum) so occluders behind /
     * beside the camera still cast shadows into view - camera-frustum culling
     * here is what made shadows pop and flicker as the view moved.
     */
    std::vector<DrawableData> shadowCasters;

    std::vector<LightData> lights;

    /// Per-frame snapshot of a ReflectionProbe component plus the entity's
    /// world position. Forward pass blends the K nearest probes per
    /// fragment weighted by distance falloff.
    struct ProbeData {
        glm::vec3   position{0.0f};
        float       radius       = 5.0f;
        float       falloffRange = 0.7f;
        float       intensity    = 1.0f;
        std::string hdrPath;
        int         bakeVersion  = 0;
        uint32_t    entityId     = 0;  ///< Stable id (entity index) for bake caching.
    };
    std::vector<ProbeData> probes;

    uint32_t viewportWidth  = 0;
    uint32_t viewportHeight = 0;
    // Top-left of the scene-render rect inside the GLFW window (ImGui
    // coords, y-down). Composite uses this to glViewport into the editor's
    // viewport child instead of covering the whole backbuffer.
    uint32_t viewportX = 0;
    uint32_t viewportY = 0;
    // Window pixel size. Needed by composite to flip ImGui-style viewportY
    // (top-down) into OpenGL glViewport (bottom-up).
    uint32_t windowWidth  = 0;
    uint32_t windowHeight = 0;

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
        uint32_t viewportHeight,
        ShadowCasterCache& shadowCache
    );

};

} // namespace Engine
