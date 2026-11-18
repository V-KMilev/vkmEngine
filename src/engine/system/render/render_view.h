#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "core/frame/camera_data.h"
#include "system/render/data/light_data.h"
#include "system/render/data/probe_data.h"
#include "core/frame/render_objects.h"
#include "system/render/data/decal_data.h"
#include "system/render/data/outline_data.h"
#include "debug/draw/debug_line.h"
#include "system/render/data/particle_data.h"
#include "system/render/data/irradiance_volume_data.h"
#include "ecs/water/wave_frame.h"
#include "io/project/render_settings.h"
#include "core/host_chrome.h"
#include "core/frame/loading_frame.h"
#include "core/frame/splash_frame.h"
#include "core/frame/ui_draw_data.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

class Scene;
class PoseBuffer;
struct Environment;
struct Visibility;
struct LiveParticles;

inline constexpr float RENDER_BAKE_BUDGET = 0.012f;  ///< What a host's frames give long bakes, in seconds:
                                                     ///< about a frame at 60, so a loading screen still
                                                     ///< moves.

/**
 * @brief What the backend draws of a frame's DebugDraw (RenderView::SceneLists::debug).
 */
struct DebugFrame {
    const std::vector<DebugLine>* lines      = nullptr;  ///< Borrowed; null or empty draws nothing
    float                         pointScale = 1.0f;     ///< Pixels a point: the display's density by the
                                                         ///< host's scale
};

/**
 * @brief The backend-agnostic view of the frame handed to RenderBackend::render.
 *
 * The whole engine -> backend contract besides the asset graph. build() refills
 * it each frame: what it gathers from the scene it owns, in @ref lists; other systems' products
 * it borrows by pointer, valid until the next build().
 *
 * A member added here fails the build at the structured bindings the renderer's asset sync names
 * every member in on purpose, asking whether it carries an asset handle to make resident.
 */
struct RenderView {
    /**
     * @brief What build() gathers through the camera: emptied, not left stale, without one.
     */
    struct SceneLists {
        std::vector<LightData>     lights;               ///< The enabled ones, in world space.
        std::optional<uint32_t>    keyIndex;             ///< The key light (findKeyLight) in `lights`;
                                                         ///< none when the scene has none. See keyLight().
        std::vector<ProbeData>     probes;
        std::vector<DecalData>     decals;               ///< Every enabled one the camera sees, a material's
                                                         ///< together.
        std::vector<OutlineData>   outlines;             ///< Every enabled Outline whose mesh the eye sees.
        std::vector<ParticleData>  particlesAdditive;    ///< Order-independent.
        std::vector<ParticleData>  particlesAlpha;       ///< Unsorted; the backend orders them.
        std::vector<ParticleData>  particlesDistortion;  ///< Unsorted; the backend orders them.
        std::vector<StripPoint>    stripPoints;          ///< Every strip's points, a strip's together.
        std::vector<ParticleStrip> stripsAdditive;       ///< Trails; order-independent.
        std::vector<ParticleStrip> stripsAlpha;          ///< Unsorted; the backend orders them.
        std::vector<ParticleStrip> stripsDistortion;     ///< Unsorted; the backend orders them.

        std::vector<TextureHandle> particleTextures;  ///< What they show, by ParticleData::params.z or
                                                      ///< ParticleStrip::texture, less one, and an alpha
                                                      ///< billboard's normal map by ParticleData::uvNext.w.

        IrradianceVolumeData irradianceVolume{};  ///< The scene's baked-GI volume, meaningful while
                                                  ///< hasIrradianceVolume. One only; chosen by
                                                  ///< findIrradianceVolume().
        bool                 hasIrradianceVolume = false;

        WaveFrame waves{};  ///< The scene's waves at the frame's moment (WaterData::waves); still with no
                            ///< water.

        const std::vector<glm::mat4>* skinMatrices = nullptr;  ///< The frame's bone palettes:
                                                               ///< PoseBuffer::palette(), borrowed whole,
                                                               ///< one flat buffer an object finds its bones
                                                               ///< in through `skinFirst`. Null on a frame
                                                               ///< that posed nothing, so a backend can skip
                                                               ///< all skinned work.

        DebugFrame debug;  ///< What DebugDraw holds this frame, set by RenderSystem; drawn over the scene.

        /// The scene's key light among `lights`, or null when it has none.
        const LightData* keyLight() const { return keyIndex ? &lights[*keyIndex] : nullptr; }
    };

    uint32_t viewportX      = 0;  ///< Viewport rect within the backbuffer, top-left origin (surfaceHeight
                                  ///< flips it).
    uint32_t viewportY      = 0;
    uint32_t viewportWidth  = 0;
    uint32_t viewportHeight = 0;
    uint32_t surfaceWidth   = 0;  ///< Full backbuffer width.
    uint32_t surfaceHeight  = 0;  ///< Full backbuffer height.

    float frameSeconds = 0.0f;  ///< Wall-clock seconds since the last frame, paused or not: what presentation
                                ///< that adapts over time (auto-exposure) steps by.

    float bakeBudget = 0.0f;  ///< Seconds of CPU a frame's long bakes may take before they go on the next
                              ///< frame: a GPU bake takes a step a frame while it is above 0, and 0 bakes
                              ///< whole. RenderSystem gives RENDER_BAKE_BUDGET, so a world's first light
                              ///< comes over frames a loading screen can show; a frame read back at once (a
                              ///< test, a capture) wants it whole.

    double worldSeconds = 0.0;  ///< The world's clock, as NetSession::worldTime gives it: what a flickering
                                ///< light wavers by.

    bool worldArriving = false;  ///< Whether a world just arrived still waits on its meshes and textures,
                                 ///< which no capture is taken again for once they land, or on its sky's
                                 ///< image (RenderSystem's ArrivalHold): a capture now would be of a world
                                 ///< part-made. A backend begins no bake meanwhile and owes one
                                 ///< (RenderReadiness::total).

    CameraData camera;             ///< The view Visibility culled for: the scene's camera or a host's.
    bool       hasCamera = false;  ///< Whether a camera resolved this frame. Without one @ref lists is empty
                                   ///< and says nothing about the scene, so a backend keeps its per-probe or
                                   ///< per-volume captures.

    const RenderObjects* objects = nullptr;  ///< Every object the frame can draw, and the lists naming which
                                             ///< each reader draws. Borrowed from the cull's Visibility.
                                             ///< Never null once build() has run.

    SceneLists lists;  ///< The lights, probes, decals, particles, volume and waves the camera is given.

    const RenderSettings* settings    = nullptr;  ///< Graphics's; set by RenderSystem, never null in a
                                                  ///< frame a backend draws.
    HostChrome::Display   display;                ///< What the host has it display: a debug view, the
                                                  ///< grid.
    const Environment*    environment = nullptr;  ///< The scene's: its sky, night sky and fog. Never null
                                                  ///< once build() has run.
    const UIDrawData*     ui          = nullptr;  ///< Borrowed; null when the UISystem has not run.
    const SplashFrame*    splash      = nullptr;  ///< The startup logo, SplashSystem's; null with none.
    const UIDrawData*     cover       = nullptr;  ///< The loading screen's draw list, over the splash and
                                                  ///< under its bar; borrowed, null for none.
    const LoadingFrame*   loading     = nullptr;  ///< The loading screen's fade, bar and backdrop,
                                                  ///< LoadingSystem's; set by RenderSystem, null with
                                                  ///< none.
    glm::vec2             pointer{0.0f};          ///< The pointer, in backbuffer pixels from the top left:
                                                  ///< what a screen may answer the mouse by.

    uint64_t worldEpoch = 0;  ///< Scene::epoch() at build time: which world these items came from. A replaced
                              ///< world reuses entity slots and poses, so a backend's baked captures need
                              ///< this to tell they belong to a scene that is gone.

    public:
        /**
         * @brief Refill the view for the current frame.
         *
         * With no active camera @ref lists is emptied (cleared, not
         * stale); @p ui, @p cover and @p splash still pass through.
         *
         * @param scene      Scene the lights, probes, decals, emitters, volume and waves are gathered
         *                   from.
         * @param visibility This frame's cull: its camera, and the borrowed @ref objects.
         * @param ui The UISystem's draw list, or null.
         * @param cover The loading screen's draw list (UISystem), or null.
         * @param splash The SplashSystem's product, or null once the sequence is over.
         * @param poses This frame's pose, or null (or empty) when nothing posed:
         *              objects then draw bind poses.
         * @param particles ParticleSystem's live particles, or null if it has not run.
         */
        void build(
            const Scene& scene,
            const Visibility& visibility,
            const UIDrawData* ui,
            const UIDrawData* cover,
            const SplashFrame* splash,
            const PoseBuffer* poses,
            const LiveParticles* particles
        );

        /// Whether the splash shows this frame.
        bool splashShows() const { return splash && splash->isShowing(); }

        /// Whether the loading screen's backdrop and bar draw: it shows, and no splash does.
        bool loadingBackdropShows() const { return loading && loading->isShowing() && !splashShows(); }

        /// Whether the loading screen hides the whole frame, so nothing of the world shows.
        bool loadingCovers() const { return loading && loading->opacity >= 1.0f; }

        /// The scene's width in pixels: the viewport's at settings.renderScale.
        uint32_t sceneWidth() const { return settings->sceneExtent(viewportWidth); }

        /// The scene's height in pixels: the viewport's at settings.renderScale.
        uint32_t sceneHeight() const { return settings->sceneExtent(viewportHeight); }
};

} // namespace Vkm::Engine
