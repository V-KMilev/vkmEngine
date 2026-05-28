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

// Each renderer subsystem owns a small config sub-struct. Adding a new
// effect adds a sub-struct, not another flat field; each sub-struct can
// gain or shed members without churn on unrelated effects' callers; and
// the JSON persistence groups settings as nested objects so future
// per-effect versioning (a "BloomConfigV2" deprecating a field) doesn't
// require touching every other effect's persisted layout.

struct AmbientConfig {
    glm::vec3 color     = glm::vec3(1.0f);
    float     intensity = 0.03f;
};

struct IBLConfig {
    std::string path      = "";   ///< Empty = no IBL bake; ambient fallback applies.
    float       intensity = 1.0f;
};

struct AOConfig {
    bool  enabled   = true;
    float radius    = 0.5f;
    float intensity = 0.8f;        ///< Full-strength GTAO tends to over-darken contact.
};

struct SSRConfig {
    bool  enabled     = true;
    float intensity   = 0.6f;
    float maxDistance = 8.0f;      ///< View-space ray length.
    float thickness   = 0.5f;      ///< View-space hit tolerance.
};

struct TAAConfig {
    bool  enabled = false;
    float blend   = 0.5f;          ///< History weight (only used when enabled).
};

struct DofConfig {
    bool  enabled       = false;
    float focusDistance = 5.0f;
    float focusRange    = 50.0f;
    float maxBlur       = 0.001f;  ///< Gather radius in UV.
};

struct MotionBlurConfig {
    bool  enabled  = false;
    float strength = 0.5f;
};

struct StarburstConfig {
    bool  enabled   = false;
    float intensity = 1.0f;
};

struct LensFlareConfig {
    bool  enabled         = false;
    float intensity       = 0.15f;   ///< Additive linear HDR; bloom + tonemap amplify.
    float threshold       = 4.0f;    ///< HDR luminance floor - excludes diffuse sky.
    int   ghostCount      = 6;       ///< Ghosts along the optical axis (1..8).
    float ghostSpacing    = 0.2f;    ///< UV step between ghosts.
    float haloRadius      = 0.3f;    ///< UV distance from source to halo ring.
    float chromatic       = 0.003f;  ///< Per-channel UV offset for fringe.
    StarburstConfig starburst;
};

struct LensDirtConfig {
    bool  enabled   = false;
    float intensity = 0.4f;
};

struct BloomConfig {
    float strength  = 0.01f;         ///< Linear-HDR blend before exposure + tonemap.
    float threshold = 0.0f;          ///< Brightness below which a pixel contributes nothing.
    float knee      = 0.0f;          ///< Half-width of the soft-knee transition around threshold.
};

/**
 * @brief Hi-Z pyramid build + visibility-time occlusion test.
 *
 * @p useHiZ builds the max-Z depth pyramid each frame and uses the
 * previous frame's pyramid as the visibility-time occlusion oracle.
 * 1-frame stale, conservative (visible on miss). Off by default so
 * pop-in regressions on fast camera motion are opt-in.
 */
struct OcclusionConfig {
    bool useHiZ = false;
};

/**
 * @brief Order-independent transparency mode.
 *
 * When @p useOIT is true the transparent forward phase writes to a
 * Weighted-Blended OIT pair (accum + revealage) and a resolve pass
 * composites the result on top of the opaque+sky scene (McGuire-Bavoil
 * 2013). Off by default; the sorted back-to-front path is required for
 * refractive (transmissive) materials, which OIT cannot reproduce.
 */
struct TransparencyConfig {
    bool useOIT = false;
};

/**
 * @brief Shadow atlas sizing + PCF kernel width.
 *
 * Atlas resolutions retune at runtime via GLShadowAtlas::ensureResolution.
 * @p softness multiplies the 12-tap Poisson kernel: the 2D array path
 * widens the 1.5-texel disk, the cube path jitters sample direction in
 * the plane perpendicular to the light with offsets that scale with
 * distance so closer fragments get a wider penumbra.
 */
struct ShadowConfig {
    uint32_t atlasRes2D   = 2048;
    uint32_t atlasResCube = 512;
    float    softness     = 0.0f;
};

struct ExposureConfig {
    bool  autoExposure = false;      ///< Off by default - matches glTF reference viewers.
    float key          = 0.18f;      ///< Target middle-gray.
    /// Adaptation rate when the scene gets BRIGHTER (high default - the
    /// pupil constricts fast). Per-second exponential ease.
    float speedBrighten = 5.0f;
    /// Adaptation rate when the scene gets DARKER (low default - rod
    /// recovery in the eye is slow, and a fast dark-adapt looks fake).
    float speedDarken   = 1.0f;
    float min          = 0.03f;      ///< Clamp on the derived exposure.
    float max          = 8.0f;
};

struct ColorGradeConfig {
    bool        enabled   = false;
    std::string lutPath   = "";
    float       intensity = 1.0f;
};

struct GridConfig {
    bool  enabled   = true;
    float size      = 1000.0f;
    float scale     = 1.0f;
    float fadeStart = 50.0f;
    float fadeEnd   = 550.0f;
};

struct AABBDebugConfig {
    bool      enabled = false;
    glm::vec3 color   = glm::vec3(1.0f, 0.0f, 0.0f);
};

struct SelectionOutlineConfig {
    bool      enabled   = true;
    glm::vec3 color     = glm::vec3(0.29f, 0.62f, 1.0f);  ///< Editor accent blue.
    float     thickness = 2.5f;                            ///< Outline width in screen pixels.
};

/**
 * @brief Top-level rendering mode.
 *
 * Default is the photoreal pipeline. Other entries are diagnostic views
 * that swap large parts of the pipeline (forward shading, post chain,
 * display transform) for inspection-friendly behavior. Resolved into a
 * RenderModeConfig once per frame so passes don't have to know which
 * specific mode triggered which decision.
 */
enum class RenderMode : uint8_t {
    Default = 0,
    Unlit,                   ///< Albedo + emission only; no lighting, no post.
    Wireframe,               ///< Unlit lines through PBR meshes; post chain bypassed.
    WireframeOverShaded,     ///< Full PBR with a wireframe overlay drawn on top.
    Normals,                 ///< World-space normal as RGB (normal*0.5+0.5).
    Depth,                   ///< Linearised camera-space depth as grayscale.
    AOOnly,                  ///< GTAO factor as grayscale (white = fully lit).
    LightingOnly,            ///< PBR with material forced to neutral (0.5 albedo, 0 metal, 0.5 rough).
    Roughness,               ///< Surface roughness as grayscale (after texture sample).
    Metallic,                ///< Surface metallic factor as grayscale.
    Emission,                ///< Raw emission (linear HDR, no display transform).
    TangentSpace,            ///< Tangent vector as RGB.
    AlbedoOnly,              ///< Sampled base color with no lighting.
    WorldPosition,           ///< fract(worldPos) as RGB (1m grid).
    UV,                      ///< UV channel 0 as red/green.
    Overdraw,                ///< Heatmap of per-pixel shaded-fragment count (additive blend).
    BatchId,                 ///< Hashed RGB per InstanceBatcher batch index.
    LightComplexity,         ///< Per-fragment count of contributing lights (turbo ramp).
    LightmapUV,              ///< Reserved for UV channel 1 - stand-in pattern (no UV1 today).
};

/**
 * @brief Pass-facing facts derived from a RenderMode.
 *
 * Single source of truth for "is the post chain off this frame?", "is
 * the forward pass drawing wireframe?", "should composite bypass the
 * display transform?" - resolved by RenderSystem::update once per
 * frame and read directly by each consumer. Adding a new diagnostic
 * mode is one place: the resolveModeConfig() switch grows; every pass
 * already reads the right boolean.
 */
struct RenderModeConfig {
    RenderMode mode = RenderMode::Default;

    /// Disables HDR-altering post passes (SSR, Bloom, LensFlare, TAA,
    /// DoF, MotionBlur, Exposure) so the diagnostic view is clean.
    bool disablePost = false;

    /// Force-off the PBR shader's AO sample for the frame. The gbuffer
    /// AO is computed from filled-triangle geometry; in wireframe that
    /// AO doesn't correspond to what's actually drawn.
    bool disableSSAO = false;

    /// Composite skips exposure + tonemap + LUT, just sRGB-encodes the
    /// linear HDR. Used so diagnostic colors show pixel-exact.
    bool bypassDisplayXform = false;

    /// Forward pass routes every batch through the unlit shader.
    bool forceUnlit = false;

    /// Forward pass enables GL_LINE polygon mode for the geometry phase.
    bool wireframe = false;

    /// Forward pass runs a second wireframe draw on top of the shaded scene.
    /// Independent from `wireframe`: that one replaces the fill with lines,
    /// this one overlays lines over the shaded fill.
    bool wireframeOverlay = false;

    /// Forward pass overrides every material with a neutral PBR (albedo=0.5,
    /// metallic=0, roughness=0.5) so the lighting term becomes visible without
    /// material colour noise. Pushed to the PBR shader as u_forceNeutralMaterial.
    bool forceNeutralMaterial = false;

    /**
     * @brief Additive-blend mode for the Overdraw diagnostic.
     *
     * Forward pass switches to (GL_ONE, GL_ONE) with depth-test off so every
     * shaded fragment accumulates into the HDR target - the resulting
     * saturation reads as a heatmap of overdraw. Orthogonal to the wireframe
     * / OIT blend paths; those modes take precedence in the inspector.
     */
    bool overdrawBlend = false;

    /**
     * @brief PBR fragment-shader diagnostic selector.
     *
     * 0 = normal output; 1 = Normals, 2 = Depth, 3 = AO, 4 = Roughness,
     * 5 = Metallic, 6 = Emission, 7 = TangentSpace, 8 = AlbedoOnly,
     * 9 = WorldPosition, 10 = UV, 11 = Overdraw, 12 = BatchId,
     * 13 = LightComplexity, 14 = LightmapUV. Pushed as u_debugMode; the
     * shader's main() branches just before FragColor is written so the
     * diagnostic value lands in the HDR target and bypassDisplayXform
     * carries it through composite.
     */
    int debugMode = 0;
};

/**
 * @brief Backend-agnostic environment/scene settings.
 *
 * Written by the editor, read by backend passes during rendering.
 * Lives on RenderSystem, copied into RenderView each frame. Composed of
 * per-effect sub-configs (see above) so each subsystem owns its own
 * settings shape independently.
 */
struct EnvironmentConfig {
    AmbientConfig    ambient;
    IBLConfig        ibl;
    ShadowConfig     shadow;
    TransparencyConfig transparency;
    OcclusionConfig  occlusion;

    AOConfig         ao;
    SSRConfig        ssr;
    TAAConfig        taa;
    DofConfig        dof;
    MotionBlurConfig motionBlur;
    LensFlareConfig  lensFlare;
    LensDirtConfig   lensDirt;
    BloomConfig      bloom;
    ExposureConfig   exposure;
    ColorGradeConfig colorGrade;

    GridConfig       grid;
    AABBDebugConfig  aabbDebug;
    SelectionOutlineConfig selection;

    /// Display transform / tone-mapping curve owned by the composite pass.
    /// 0 = AgX, 1 = Khronos PBR Neutral (default), 2 = ACES, 3 = Reinhard.
    int        tonemap    = 1;
    /// Background clear color (linear HDR).
    glm::vec4  clearColor = glm::vec4(0.1f, 0.1f, 0.12f, 1.0f);
    /// Top-level render mode (Default / Wireframe / future debug views).
    /// Resolved into a RenderModeConfig in RenderView each frame; passes
    /// read facts from view.modeConfig, not this enum directly.
    RenderMode renderMode = RenderMode::Default;
};

/// Translate an env's RenderMode into the boolean facts passes read.
inline RenderModeConfig resolveModeConfig(RenderMode mode) {
    RenderModeConfig c;
    c.mode = mode;
    switch (mode) {
        case RenderMode::Unlit:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.forceUnlit         = true;
            break;
        case RenderMode::Wireframe:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.forceUnlit         = true;
            c.wireframe          = true;
            break;
        case RenderMode::WireframeOverShaded:
            c.wireframeOverlay   = true;
            break;
        case RenderMode::Normals:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 1;
            break;
        case RenderMode::Depth:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 2;
            break;
        case RenderMode::AOOnly:
            c.disablePost        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 3;
            break;
        case RenderMode::LightingOnly:
            c.disablePost        = true;
            c.bypassDisplayXform = true;
            c.forceNeutralMaterial = true;
            break;
        case RenderMode::Roughness:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 4;
            break;
        case RenderMode::Metallic:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 5;
            break;
        case RenderMode::Emission:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 6;
            break;
        case RenderMode::TangentSpace:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 7;
            break;
        case RenderMode::AlbedoOnly:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 8;
            break;
        case RenderMode::WorldPosition:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 9;
            break;
        case RenderMode::UV:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 10;
            break;
        case RenderMode::Overdraw:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.overdrawBlend      = true;
            c.debugMode          = 11;
            break;
        case RenderMode::BatchId:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 12;
            break;
        case RenderMode::LightComplexity:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 13;
            break;
        case RenderMode::LightmapUV:
            c.disablePost        = true;
            c.disableSSAO        = true;
            c.bypassDisplayXform = true;
            c.debugMode          = 14;
            break;
        case RenderMode::Default:
            break;
    }
    return c;
}

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
        uint32_t viewportHeight
    );

};

} // namespace Engine
