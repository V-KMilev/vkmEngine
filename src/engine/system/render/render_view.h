#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "system/render/data/camera_data.h"
#include "system/render/data/light_data.h"
#include "system/render/data/planar_reflector_data.h"
#include "system/render/data/probe_data.h"
#include "system/render/data/render_objects.h"
#include "system/render/data/decal_data.h"
#include "system/render/data/debug_line_data.h"
#include "system/render/data/particle_data.h"
#include "system/render/data/irradiance_volume_data.h"
#include "system/ocean/ocean_data.h"
#include "system/render/render_settings.h"
#include "system/splash/splash_frame.h"
#include "system/ui/ui_draw_data.h"
#include "ecs/environment.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

class Scene;
class PoseBuffer;
struct Visibility;
struct LiveParticles;

/// What a host's frames give long bakes, in seconds: about a frame at 60, so a loading screen still moves.
inline constexpr float RENDER_BAKE_BUDGET = 0.012f;

/**
 * @brief The backend-agnostic view of the frame handed to RenderBackend::render.
 *
 * The whole engine -> backend contract besides the asset graph. build() refills
 * it each frame: what it gathers from the scene it owns; other systems' products
 * it borrows by pointer, valid until the next build().
 *
 * A member added here fails the build at two structured bindings that name every member on
 * purpose - GLView::sync and build()'s no-camera branch - each asking what the new member means
 * there: an asset handle to make resident, a list to clear without a camera.
 */
struct RenderView {
    /// Viewport rect within the backbuffer, top-left origin (surfaceHeight flips it).
    uint32_t viewportX      = 0;
    uint32_t viewportY      = 0;
    uint32_t viewportWidth  = 0;
    uint32_t viewportHeight = 0;
    uint32_t surfaceWidth   = 0;  ///< Full backbuffer width.
    uint32_t surfaceHeight  = 0;  ///< Full backbuffer height.

    /// Wall-clock seconds since the last frame, paused or not: what presentation that adapts
    /// over time (auto-exposure) steps by.
    float frameSeconds = 0.0f;

    /**
     * @brief Seconds of CPU a frame's long bakes may take before they go on the next frame.
     *
     * A bake that is the GPU's work takes a step a frame while it is above 0; 0 bakes whole.
     * RenderSystem gives RENDER_BAKE_BUDGET so a world's first light comes over frames a loading
     * screen can show; a frame read back at once (a test, a capture) wants it whole.
     */
    float bakeBudget = 0.0f;

    /// The world's clock, as NetSession::worldTime gives it: what a flickering light wavers by.
    double worldSeconds = 0.0;

    /// Whether a world just arrived still waits on its meshes or textures (RenderSystem's
    /// ArrivalHold): a capture of it now would be of a world part-made, and none is taken again
    /// when they land. A backend begins no bake meanwhile and owes one (RenderReadiness::total).
    bool worldArriving = false;

    CameraData camera;  ///< The view Visibility culled for: the scene's camera or a host's.
    /**
     * @brief Whether a camera resolved this frame.
     *
     * Without one the scene lists are left empty and say nothing about the
     * scene, so a backend keeps its per-probe or per-volume captures.
     */
    bool       hasCamera = false;

    /**
     * @brief Every object the frame can draw, and the lists naming which each reader draws.
     *
     * Borrowed from the cull's Visibility. Never null once build() has run.
     */
    const RenderObjects* objects = nullptr;

    std::vector<LightData>            lights;  ///< The enabled ones, in world space.
    std::vector<ProbeData>            probes;
    std::vector<PlanarReflectorData>  planarReflectors;  ///< The enabled ones; the backend picks.
    std::vector<DecalData>            decals;  ///< Every enabled one the camera sees, a material's together.
    std::vector<ParticleData>         particlesAdditive;    ///< Order-independent.
    std::vector<ParticleData>         particlesAlpha;       ///< Unsorted; the backend orders them.
    std::vector<ParticleData>         particlesDistortion;  ///< Unsorted; the backend orders them.
    std::vector<StripPoint>           stripPoints;          ///< Every strip's points, a strip's together.
    std::vector<ParticleStrip>        stripsAdditive;       ///< Ribbons and trails; order-independent.
    std::vector<ParticleStrip>        stripsAlpha;          ///< Unsorted; the backend orders them.
    std::vector<ParticleStrip>        stripsDistortion;     ///< Unsorted; the backend orders them.

    /// What they show, by ParticleData::params.z or ParticleStrip::texture, less one, and an alpha
    /// billboard's normal map by ParticleData::uvNext.w.
    std::vector<TextureHandle> particleTextures;

    /**
     * @brief The scene's baked-GI volume, meaningful while @ref hasIrradianceVolume.
     *
     * One only (see GLIrradianceVolume); chosen by findIrradianceVolume.
     */
    IrradianceVolumeData irradianceVolume{};
    bool                 hasIrradianceVolume = false;

    /// The scene's sea at the frame's moment (OceanSystem); not present without a camera.
    OceanData ocean{};

    /**
     * @brief The frame's bone palettes: PoseBuffer::palette(), borrowed whole.
     *
     * One flat buffer; an object finds its bones through `skinFirst`. Null on a
     * frame that posed nothing, so a backend can skip all skinned work.
     */
    const std::vector<glm::mat4>* skinMatrices = nullptr;

    /// What DebugDraw holds this frame, set by RenderSystem; drawn over the scene with a camera.
    DebugFrame debug;

    RenderSettings                settings;
    /// The scene's, copied whole; the backend reads its sky, night sky and fog.
    Environment                   environment;
    const UIDrawData*             ui = nullptr;  ///< Borrowed; null when the UISystem has not run.
    SplashFrame                   splash;        ///< The startup logo, or nothing once it is over.
    /// The loading screen's draw list, over the splash and under its bar; borrowed, null for none.
    const UIDrawData*             cover = nullptr;
    /// How much the loading screen shows, how far its bar has come in and how far it is filled:
    /// LoadingFrame's opacity, content and progress.
    float                         loadingOpacity  = 0.0f;
    float                         loadingContent  = 0.0f;
    float                         loadingProgress = 0.0f;
    /// Whether the engine's water is the loading screen's backdrop (LoadingFrame::water); else black.
    bool                          loadingWater    = true;
    /// The pointer, in backbuffer pixels from the top left: what a screen may answer the mouse by.
    glm::vec2                     pointer{0.0f};

    /**
     * @brief Scene::epoch() at build time: which world these items came from.
     *
     * A replaced world reuses entity slots and poses, so a backend's baked
     * captures need this to tell they belong to a scene that is gone.
     */
    uint64_t worldEpoch = 0;

    public:
        /**
         * @brief Refill the view for the current frame.
         *
         * With no active camera the 3D half is emitted empty (cleared, not
         * stale); @p ui, @p cover and @p splash still pass through.
         *
         * @param scene      Scene the lights, probes, decals, emitters and volume are gathered from.
         * @param visibility This frame's cull: its camera, and the borrowed @ref objects.
         * @param ui The UISystem's draw list, or null.
         * @param cover The loading screen's draw list (UISystem), or null.
         * @param splash The SplashSystem's product, or null once the sequence is over.
         * @param poses This frame's pose, or null (or empty) when nothing posed:
         *              objects then draw bind poses.
         * @param particles ParticleSystem's live particles, or null if it has not run.
         * @param ocean     OceanSystem's sea, or null if it has not run.
         */
        void build(
            const Scene& scene,
            const Visibility& visibility,
            const UIDrawData* ui,
            const UIDrawData* cover,
            const SplashFrame* splash,
            const PoseBuffer* poses,
            const LiveParticles* particles,
            const OceanData* ocean
        );

        /// Whether the loading screen's backdrop and bar draw: it shows, and no splash does.
        bool loadingBackdropShows() const { return loadingOpacity > 0.0f && !splash.isShowing(); }

        /// The scene's width in pixels: the viewport's at settings.renderScale.
        uint32_t sceneWidth() const { return settings.sceneExtent(viewportWidth); }

        /// The scene's height in pixels: the viewport's at settings.renderScale.
        uint32_t sceneHeight() const { return settings.sceneExtent(viewportHeight); }
};

} // namespace Vkm::Engine
