#pragma once

#include <cstdint>
#include <vector>

#include "system/render/data/camera_data.h"
#include "system/render/data/drawable_data.h"
#include "system/render/data/light_data.h"
#include "system/render/data/shadow_caster_data.h"
#include "system/render/data/probe_data.h"
#include "system/render/render_settings.h"
#include "ecs/environment.h"

namespace Engine {

class Scene;
struct Visibility;

/**
 * @brief The backend-agnostic snapshot handed to RenderBackend::render each frame.
 *
 * This is the whole engine -> backend contract: every backend consumes exactly
 * this struct, which is what makes them interchangeable. build() refills it from
 * the visible set the VisibilitySystem already produced, reusing the vectors'
 * capacity across frames.
 */
struct RenderView {
    uint32_t viewportX      = 0;                   ///< The x-coordinate of the viewport.
    uint32_t viewportY      = 0;                   ///< The y-coordinate of the viewport.
    uint32_t viewportWidth  = 0;                   ///< The width of the viewport.
    uint32_t viewportHeight = 0;                   ///< The height of the viewport.
    uint32_t surfaceHeight  = 0;                   ///< Full backbuffer height the viewport rect sits within (lets a bottom-left backend flip the rect).

    CameraData camera;                             ///< The camera data for the view.
    std::vector<DrawableData>     drawables;       ///< The drawables for the view.
    std::vector<ShadowCasterData> shadowCasters;   ///< The shadow casters for the view.
    std::vector<LightData>        lights;          ///< The lights for the view.
    std::vector<ProbeData>        probes;          ///< The reflection probes in the scene.

    RenderSettings                settings;        ///< Editable render tuning (copied from the RenderSystem each frame).
    Environment                   environment;     ///< Lighting environment (HDR/skybox), copied from the Scene each frame in build().

    public:
        /**
         * @brief Refill the snapshot for the current frame.
         *
         * Copies the scene's environment, then rebuilds camera, lights, probes,
         * drawables, and shadow casters from the already-culled @p visibility
         * set, reusing the vectors' capacity. With no active camera this frame it
         * emits an empty snapshot (cleared, not stale) and returns early.
         */
        void build(
            const Scene& scene,
            const Visibility& visibility
        );

    private:
        /** @brief Flatten the active camera's matrices + position; precomputes invProjection. */
        void buildCamera(const Visibility& visibility);
        /** @brief Snapshot every enabled light to world space (incl. area-light axes). */
        void buildLights(const Scene& scene);
        /** @brief Snapshot every reflection probe to world space. */
        void buildProbes(const Scene& scene);
        /** @brief Snapshot one drawable per visible entity with a resolved mesh+material. */
        void buildDrawables(const Scene& scene, const Visibility& visibility);
        /** @brief Snapshot the scene-wide shadow-caster set with world AABBs. */
        void buildShadowCasters(const Scene& scene, const Visibility& visibility);
};

} // namespace Engine
