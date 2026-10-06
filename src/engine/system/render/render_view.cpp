#include "system/render/render_view.h"

#include <algorithm>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/environment.h"
#include "system/animation/pose_buffer.h"
#include "system/particle/live_particles.h"
#include "system/visibility/visibility.h"

#include "debug/profiler.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Snapshot every enabled light into world space, area-light axes included.
 *
 * @param view  The view being refilled.
 * @param scene Scene whose Light components are gathered.
 */
void buildLights(RenderView& view, const Scene& scene) {
    PROFILE_SCOPE("RenderView::buildLights");
    view.lights.clear();
    view.lights.reserve(scene.count<Light>());

    scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& transform) {
        if (!light.enabled) return;

        const glm::vec3 position = resolvedWorldPosition(scene, id, transform);
        const glm::quat rotation = resolvedWorldRotation(scene, id, transform);

        LightData data{};  // area-light fields stay zero for punctual lights
        data.type       = light.type;
        data.entitySlot = id.slot();
        data.color      = light.color;
        data.intensity  = light.intensity;
        data.position   = position;
        data.direction  = Math::computeForward(rotation);

        data.radius         = light.radius;
        data.innerConeAngle = light.innerConeAngle;
        data.outerConeAngle = light.outerConeAngle;

        data.castShadows      = light.castShadows;
        data.shadowBias       = light.shadowBias;
        data.shadowNormalBias = light.shadowNormalBias;
        data.shadowDistance   = light.shadowDistance;
        // Held to 45 degrees of radius: the tangent the disc is sized by diverges at 90.
        data.sourceRadius = light.type == LightType::Directional
            ? std::clamp(light.sourceRadius, 0.0f, glm::quarter_pi<float>())
            : std::max(light.sourceRadius, 0.0f);

        // World-space half-extent axes. Disk uses areaRadius on both so cross(U, V) stays a clean normal.
        if (light.type == LightType::Rect || light.type == LightType::Disk) {
            const glm::vec3 right = Math::computeRight(rotation);
            const glm::vec3 up    = Math::computeUp(rotation);
            if (light.type == LightType::Rect) {
                data.axisU = right * (light.areaWidth  * 0.5f);
                data.axisV = up    * (light.areaHeight * 0.5f);
            } else {
                data.axisU = right * light.areaRadius;
                data.axisV = up    * light.areaRadius;
            }
            data.twoSided = light.twoSided;
        }

        view.lights.push_back(data);
    });
}

/**
 * @brief Snapshot every reflection probe into world space.
 *
 * @param view  The view being refilled.
 * @param scene Scene whose ReflectionProbe components are gathered.
 */
void buildProbes(RenderView& view, const Scene& scene) {
    view.probes.clear();

    scene.forEach<ReflectionProbe, Transform>(
        [&](EntityId id, const ReflectionProbe& probe, const Transform& transform) {
            const glm::vec3 position = resolvedWorldPosition(scene, id, transform);
            view.probes.push_back({
                position,
                probe.halfExtents,
                probe.falloff,
                probe.intensity,
                probe.resolution,
                probe.bakeVersion,
                id.slot()
            });
        }
    );
}

/**
 * @brief Snapshot every decal into world space (box transform + its inverse).
 *
 * @param view  The view being refilled.
 * @param scene Scene whose Decal components are gathered.
 */
void buildDecals(RenderView& view, const Scene& scene) {
    PROFILE_SCOPE("RenderView::buildDecals");
    view.decals.clear();

    scene.forEach<Decal, Transform>([&](EntityId id, const Decal& decal, const Transform& transform) {
        if (!decal.enabled) return;

        const glm::mat4 model = resolvedWorldMatrix(scene, id, transform);
        view.decals.push_back({model, glm::inverse(model), decal.material, decal.angleFade, decal.opacity});
    });
}

/**
 * @brief Flatten every emitter's live particles into billboard instances, split by blend, unsorted.
 *
 * @param view      The view being refilled.
 * @param scene     Scene whose ParticleEmitter components are gathered.
 * @param particles The live particles of each, or null.
 */
void buildParticles(RenderView& view, const Scene& scene, const LiveParticles* particles) {
    PROFILE_SCOPE("RenderView::buildParticles");
    view.particlesAdditive.clear();
    view.particlesAlpha.clear();

    scene.forEach<ParticleEmitter>([&](EntityId id, const ParticleEmitter& emitter) {
        const std::vector<Particle>* live = particles ? particles->of(id) : nullptr;
        if (!live) return;
        auto& out = emitter.additive ? view.particlesAdditive : view.particlesAlpha;
        for (const Particle& p : *live) {
            // Age drives the size + colour ramp; the simulation already retired expired ones.
            const float t = (p.lifetime > 0.0f) ? (p.age / p.lifetime) : 1.0f;
            out.push_back({
                glm::vec4(p.position, glm::mix(emitter.startSize, emitter.endSize, t)),
                glm::mix(emitter.startColor, emitter.endColor, t),
                glm::vec4(emitter.softness, 0.0f, 0.0f, 0.0f),
            });
        }
    });
}

/**
 * @brief Snapshot the scene's irradiance volume (findIrradianceVolume's pick) into world space.
 *
 * @param view  The view being refilled.
 * @param scene Scene to take the volume from.
 */
void buildIrradianceVolume(RenderView& view, const Scene& scene) {
    view.hasIrradianceVolume = false;

    const EntityId id = findIrradianceVolume(scene);
    if (!id) return;

    const IrradianceVolume& volume    = scene.get<IrradianceVolume>(id);
    const Transform&        transform = scene.get<Transform>(id);

    // Clamped: the backend sizes the volume's textures from these.
    const glm::uvec3 res = IrradianceVolume::clampedResolution(volume);
    view.irradianceVolume = IrradianceVolumeData{
        resolvedWorldPosition(scene, id, transform),
        volume.halfExtents,
        res.x,
        res.y,
        res.z,
        volume.intensity,
        volume.blendDistance,
        volume.bakeVersion
    };
    view.hasIrradianceVolume = true;
}

} // namespace

void RenderView::build(
    const Scene& scene,
    const Visibility& visibility,
    const UIDrawData* uiData,
    const SplashFrame* splashFrame,
    const PoseBuffer* poses,
    const LiveParticles* particles
) {
    PROFILE_SCOPE("RenderView::build");

    environment = scene.environment();
    // Before the no-camera early-out: a world replaced while unrendered must
    // still reach downstream caches.
    worldEpoch  = scene.epoch();

    // UI and splash draw without a camera, so they precede the early-out too.
    ui = uiData;

    splash = splashFrame ? *splashFrame : SplashFrame{};

    // The cull publishes empty lists without a camera, so this is never stale.
    objects = &visibility.objects;

    if (!visibility.hasCamera) {
        // An empty snapshot, not a stale one; naming every member makes a new list
        // fail to compile here. The camera stands: passes read it, and zeroed is singular.
        [[maybe_unused]] auto& [
            vpX,
            vpY,
            vpW,
            vpH,
            surfaceW,
            surfaceH,
            cameraData,
            cameraPresent,
            objectLists,
            lightList,
            probeList,
            decalList,
            particlesAdd,
            particlesBlend,
            volume,
            volumePresent,
            palettes,
            renderSettings,
            env,
            uiOverlay,
            splashState,
            epoch
        ] = *this;

        cameraPresent = false;
        lightList.clear();
        probeList.clear();
        decalList.clear();
        particlesAdd.clear();
        particlesBlend.clear();
        volumePresent = false;
        palettes      = nullptr;
        return;
    }

    hasCamera = true;
    camera = visibility.camera;
    buildLights(*this, scene);
    buildProbes(*this, scene);
    buildDecals(*this, scene);
    buildParticles(*this, scene, particles);
    buildIrradianceVolume(*this, scene);

    // A buffer holding no slices posed nothing, so it is passed on as null.
    skinMatrices = (poses && !poses->slices().empty()) ? &poses->palette() : nullptr;
}

} // namespace Vkm::Engine
