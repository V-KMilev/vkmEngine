#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "system/render/data/camera_data.h"
#include "system/render/data/light_data.h"
#include "system/render/data/probe_data.h"
#include "system/render/data/render_objects.h"
#include "system/render/data/decal_data.h"
#include "system/render/data/particle_data.h"
#include "system/render/data/irradiance_volume_data.h"
#include "system/render/render_settings.h"
#include "system/splash/splash_frame.h"
#include "system/ui/ui_draw_data.h"
#include "ecs/environment.h"

namespace Vkm::Engine {

class Scene;
class PoseBuffer;
struct Visibility;
struct LiveParticles;

/**
 * @brief The backend-agnostic view of the frame handed to RenderBackend::render.
 *
 * The whole engine -> backend contract besides the asset graph. build() refills
 * it each frame: what it gathers from the scene it owns; other systems' products
 * it borrows by pointer, valid until the next build().
 */
struct RenderView {
    /// Viewport rect within the backbuffer, top-left origin (surfaceHeight flips it).
    uint32_t viewportX      = 0;
    uint32_t viewportY      = 0;
    uint32_t viewportWidth  = 0;
    uint32_t viewportHeight = 0;
    uint32_t surfaceWidth   = 0;  ///< Full backbuffer width.
    uint32_t surfaceHeight  = 0;  ///< Full backbuffer height.

    CameraData camera;
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

    std::vector<LightData>            lights;
    std::vector<ProbeData>            probes;
    std::vector<DecalData>            decals;
    std::vector<ParticleData>         particlesAdditive;  ///< Order-independent.
    std::vector<ParticleData>         particlesAlpha;     ///< Unsorted; the backend orders them.

    /**
     * @brief The scene's baked-GI volume, meaningful while @ref hasIrradianceVolume.
     *
     * One only (see GLIrradianceVolume); chosen by findIrradianceVolume.
     */
    IrradianceVolumeData irradianceVolume{};
    bool                 hasIrradianceVolume = false;

    /**
     * @brief The frame's bone palettes: PoseBuffer::palette(), borrowed whole.
     *
     * One flat buffer; an object finds its bones through `skinFirst`. Null on a
     * frame that posed nothing, so a backend can skip all skinned work.
     */
    const std::vector<glm::mat4>* skinMatrices = nullptr;

    RenderSettings                settings;
    Environment                   environment;
    const UIDrawData*             ui = nullptr;  ///< Borrowed; null when the UISystem has not run.
    SplashFrame                   splash;

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
         * stale); @p ui and @p splash still pass through.
         *
         * @param scene      Scene the lights, probes, decals, emitters and volume are gathered from.
         * @param visibility This frame's cull: its camera, and the borrowed @ref objects.
         * @param ui The UISystem's draw list, or null.
         * @param splash The SplashSystem's product, or null once the sequence is over.
         * @param poses This frame's pose, or null (or empty) when nothing posed:
         *              objects then draw bind poses.
         * @param particles ParticleSystem's live particles, or null if it has not run.
         */
        void build(
            const Scene& scene,
            const Visibility& visibility,
            const UIDrawData* ui,
            const SplashFrame* splash,
            const PoseBuffer* poses,
            const LiveParticles* particles
        );
};

} // namespace Vkm::Engine
