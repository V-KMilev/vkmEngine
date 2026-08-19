#pragma once

#include <cstdint>
#include <vector>

#include "system/render/data/camera_data.h"
#include "system/render/data/drawable_data.h"
#include "system/render/data/light_data.h"
#include "system/render/data/shadow_caster_data.h"
#include "system/render/data/probe_data.h"
#include "system/render/data/decal_data.h"
#include "system/render/data/particle_data.h"
#include "system/render/data/irradiance_volume_data.h"
#include "system/render/render_settings.h"
#include "system/ui/ui_draw_data.h"
#include "ecs/environment.h"

namespace Vkm::Engine {

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
    uint32_t viewportX      = 0;
    uint32_t viewportY      = 0;
    uint32_t viewportWidth  = 0;
    uint32_t viewportHeight = 0;
    uint32_t surfaceHeight  = 0;                   ///< Full backbuffer height the viewport rect sits within (lets a bottom-left backend flip the rect).

    CameraData camera;
    std::vector<DrawableData>     drawables;
    std::vector<ShadowCasterData> shadowCasters;
    std::vector<LightData>        lights;
    std::vector<ProbeData>        probes;
    std::vector<DecalData>        decals;
    std::vector<ParticleData>     particlesAdditive;  ///< Billboard particles from additive emitters (order-independent).
    std::vector<ParticleData>     particlesAlpha;     ///< Billboard particles from alpha emitters, sorted back-to-front.
    std::vector<IrradianceVolumeData> irradianceVolumes;  ///< Baked-GI volumes in the scene.

    RenderSettings                settings;        ///< Editable render tuning (copied from the RenderSystem each frame).
    Environment                   environment;     ///< Lighting environment (HDR/skybox), copied from the Scene each frame in build().
    UIDrawData                    ui;              ///< Screen-space UI overlay, copied from the UISystem's draw list each frame.

    public:
        /**
         * @brief Refill the snapshot for the current frame.
         *
         * The @p ui overlay is independent of the camera, so it survives the
         * no-camera path: with no active camera this frame the 3D snapshot is
         * emitted empty (cleared, not stale) and the rest is skipped.
         *
         * @param ui The UISystem's draw list for this frame, or null if none.
         */
        void build(
            const Scene& scene,
            const Visibility& visibility,
            const UIDrawData* ui
        );

    private:
        /**
         * @brief Flatten the active camera's matrices and position into CameraData.
         *
         * Precomputes invProjection once here so the backend never inverts the
         * projection per frame.
         *
         * @param visibility Culled set carrying the active camera resolved this frame.
         */
        void buildCamera(const Visibility& visibility);

        /**
         * @brief Snapshot every enabled light into world space.
         *
         * Includes the area-light axes (axisU/axisV) derived from each light's
         * world rotation.
         *
         * @param scene Scene whose Light components are gathered.
         */
        void buildLights(const Scene& scene);

        /**
         * @brief Snapshot every reflection probe into world space.
         *
         * @param scene Scene whose ReflectionProbe components are gathered.
         */
        void buildProbes(const Scene& scene);

        /**
         * @brief Snapshot every decal into world space (box transform + its inverse).
         *
         * @param scene Scene whose Decal components are gathered.
         */
        void buildDecals(const Scene& scene);

        /**
         * @brief Flatten every emitter's live particles into billboard instances,
         * evaluating size + colour from each particle's age. Alpha particles are
         * sorted back-to-front against the camera; additive ones need no order.
         *
         * @param scene Scene whose ParticleEmitter components are gathered.
         */
        void buildParticles(const Scene& scene);

        /**
         * @brief Snapshot every irradiance volume into world space.
         *
         * @param scene Scene whose IrradianceVolume components are gathered.
         */
        void buildIrradianceVolumes(const Scene& scene);

        /**
         * @brief Snapshot one drawable per visible entity that resolves a mesh and material.
         *
         * Entities with no usable mesh+material pair are skipped, so the drawable
         * count may be smaller than the visible-entity count.
         *
         * @param scene      Scene supplying mesh, material, and transform components.
         * @param visibility Culled set listing the entities to emit drawables for.
         */
        void buildDrawables(const Scene& scene, const Visibility& visibility);

        /**
         * @brief Snapshot the scene-wide shadow-caster set with world-space AABBs.
         *
         * Casters are gathered independently of the visible set so off-screen
         * occluders still contribute to shadows.
         *
         * @param scene      Scene supplying mesh and transform components.
         * @param visibility Culled set carrying the gathered shadow-caster entities.
         */
        void buildShadowCasters(const Scene& scene, const Visibility& visibility);
};

} // namespace Vkm::Engine
