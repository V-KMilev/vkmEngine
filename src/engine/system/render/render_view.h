#pragma once

#include <cstdint>
#include <vector>

#include "system/render/data/camera_data.h"
#include "system/render/data/drawable_data.h"
#include "system/render/data/light_data.h"
#include "system/render/data/shadow_caster_data.h"
#include "system/render/data/probe_data.h"
#include "system/render/render_settings.h"

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
    uint32_t surfaceHeight  = 0;                   ///< The height of the surface. Full backbuffer height the viewport rect sits within.

    CameraData camera;                             ///< The camera data for the view.
    std::vector<DrawableData>     drawables;       ///< The drawables for the view.
    std::vector<ShadowCasterData> shadowCasters;   ///< The shadow casters for the view.
    std::vector<LightData>        lights;          ///< The lights for the view.
    std::vector<ProbeData>        probes;          ///< The reflection probes in the scene.
    RenderSettings                settings;        ///< Editable render tuning (copied from the RenderSystem each frame).

    public:
        void build(
            const Scene& scene,
            const Visibility& visibility
        );

    private:
        void buildCamera(const Visibility& visibility);
        void buildLights(const Scene& scene);
        void buildProbes(const Scene& scene);
        void buildDrawables(const Scene& scene, const Visibility& visibility);
        void buildShadowCasters(const Scene& scene, const Visibility& visibility);
};

} // namespace Engine
