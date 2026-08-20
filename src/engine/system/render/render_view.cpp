#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/render_view.h"

#include <algorithm>

#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/environment.h"
#include "system/animation/pose_buffer.h"
#include "system/visibility/visibility.h"

#include "debug/profiler.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Copy one entity's bone palette into the frame's flat array.
 *
 * Shared by both gather loops because both need it and they walk different
 * sets: the visible entities and the scene-wide shadow casters. An entity that
 * poses nothing - which is every rock in the scene, and a skinned mesh with no
 * rig above it - answers a count of 0 and copies nothing.
 *
 * @param poses This frame's poses, or null when nothing posed anything.
 * @param id    Entity the item was gathered for.
 * @param out   The frame's flat palette array, appended to.
 * @param first Set to the item's first matrix in @p out.
 * @param count Set to how many bones it has, 0 when it is not posed.
 */
void appendPose(const PoseBuffer* poses, EntityId id, std::vector<glm::mat4>& out,
                uint32_t& first, uint32_t& count) {
    first = 0;
    count = 0;
    if (!poses) return;

    const PoseSlice* slice = poses->sliceOf(id.index);
    if (!slice || slice->count == 0) return;

    const std::vector<glm::mat4>& palette = poses->palette();
    first = static_cast<uint32_t>(out.size());
    count = slice->count;
    out.insert(out.end(), palette.begin() + slice->first,
                          palette.begin() + slice->first + slice->count);
}

} // namespace

void RenderView::build(
    const Scene& scene,
    const Visibility& visibility,
    const UIDrawData* uiData,
    const PoseBuffer* poses
) {
    PROFILE_SCOPE("RenderView::build");

    environment = scene.environment();

    // The UI overlay is independent of the 3D camera, so snapshot it before the
    // no-camera early-out (a HUD/menu still draws when nothing 3D is in view).
    ui.clear();
    if (uiData) {
        ui.vertices = uiData->vertices;
        ui.commands = uiData->commands;
    }

    if (!visibility.hasCamera) {
        // No camera this frame: emit an empty snapshot, not a stale one.
        drawables.clear();
        lights.clear();
        shadowCasters.clear();
        probes.clear();
        decals.clear();
        particlesAdditive.clear();
        particlesAlpha.clear();
        irradianceVolumes.clear();
        skinMatrices.clear();
        return;
    }

    buildCamera(visibility);
    buildLights(scene);
    buildProbes(scene);
    buildDecals(scene);
    buildParticles(scene);
    buildIrradianceVolumes(scene);

    // Cleared here rather than in either gather below: both append to it and
    // neither owns it, so clearing inside one would make their call order
    // load-bearing for no stated reason.
    skinMatrices.clear();
    buildDrawables(scene, visibility, poses);
    buildShadowCasters(scene, visibility, poses);
}

void RenderView::buildCamera(const Visibility& visibility) {
    camera.view       = visibility.view;
    camera.projection = visibility.projection;
    camera.position   = visibility.cameraPosition;
    camera.derive();

    camera.focusDistance = visibility.focusDistance;
    camera.dofAmount     = visibility.dofAmount;
}

void RenderView::buildLights(const Scene& scene) {
    lights.clear();
    lights.reserve(scene.count<Light>());

    scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& transform) {
        if (!light.enabled) return;

        const glm::vec3 position = resolvedWorldPosition(scene, id, transform);
        const glm::quat rotation = resolvedWorldRotation(scene, id, transform);

        LightData data{};  // zero the snapshot; area-light fields stay unset for punctual lights
        data.type      = light.type;
        data.color     = light.color;
        data.intensity = light.intensity;
        data.position  = position;
        data.direction = Math::computeForward(rotation);

        data.radius         = light.radius;
        data.innerConeAngle = light.innerConeAngle;
        data.outerConeAngle = light.outerConeAngle;

        data.castShadows    = light.castShadows;
        data.shadowBias     = light.shadowBias;
        data.shadowDistance = light.shadowDistance;

        // Fold rotation + size into world-space half-extent axes. Disk uses
        // areaRadius on both so cross(U, V) stays a clean surface normal.
        if (light.type == LightType::Rect || light.type == LightType::Disk) {
            const glm::vec3 right = rotation * glm::vec3(1, 0, 0);
            const glm::vec3 up    = rotation * glm::vec3(0, 1, 0);
            if (light.type == LightType::Rect) {
                data.axisU = right * (light.areaWidth  * 0.5f);
                data.axisV = up    * (light.areaHeight * 0.5f);
            } else {
                data.axisU = right * light.areaRadius;
                data.axisV = up    * light.areaRadius;
            }
            data.twoSided = light.twoSided;
        }

        lights.push_back(data);
    });
}


void RenderView::buildProbes(const Scene& scene) {
    probes.clear();

    scene.forEach<ReflectionProbe, Transform>(
        [&](EntityId id, const ReflectionProbe& probe, const Transform& transform) {
            const glm::vec3 position = resolvedWorldPosition(scene, id, transform);
            probes.push_back({ position, probe.halfExtents, probe.falloff, probe.intensity,
                               probe.resolution, probe.bakeVersion });
        });
}

void RenderView::buildDecals(const Scene& scene) {
    decals.clear();

    scene.forEach<Decal, Transform>(
        [&](EntityId id, const Decal& decal, const Transform& transform) {
            const glm::mat4 model = resolvedWorldMatrix(scene, id, transform);
            decals.push_back({ model, glm::inverse(model), decal.material, decal.angleFade, decal.opacity });
        });
}

void RenderView::buildParticles(const Scene& scene) {
    particlesAdditive.clear();
    particlesAlpha.clear();

    scene.forEach<ParticleEmitter>([&](EntityId, const ParticleEmitter& emitter) {
        auto& out = emitter.additive ? particlesAdditive : particlesAlpha;
        for (const Particle& p : emitter.particles) {
            // Age drives the size + colour ramp; a particle past its life was
            // already retired by the simulation.
            const float t = (p.lifetime > 0.0f) ? (p.age / p.lifetime) : 1.0f;
            out.push_back({
                glm::vec4(p.position, glm::mix(emitter.startSize, emitter.endSize, t)),
                glm::mix(emitter.startColor, emitter.endColor, t),
                glm::vec4(emitter.softness, 0.0f, 0.0f, 0.0f),
            });
        }
    });

    // Alpha particles blend order-dependently, so sort far-to-near like the
    // transparent bucket. Additive blending is commutative - leave it unsorted.
    const glm::vec3 eye = camera.position;
    std::sort(particlesAlpha.begin(), particlesAlpha.end(),
              [&eye](const ParticleData& a, const ParticleData& b) {
                  return glm::dot(glm::vec3(a.positionSize) - eye, glm::vec3(a.positionSize) - eye)
                       > glm::dot(glm::vec3(b.positionSize) - eye, glm::vec3(b.positionSize) - eye);
              });
}

void RenderView::buildIrradianceVolumes(const Scene& scene) {
    irradianceVolumes.clear();

    scene.forEach<IrradianceVolume, Transform>(
        [&](EntityId id, const IrradianceVolume& v, const Transform& transform) {
            const glm::vec3 center = resolvedWorldPosition(scene, id, transform);
            irradianceVolumes.push_back({ center, v.halfExtents,
                                          v.resolutionX, v.resolutionY, v.resolutionZ,
                                          v.intensity, v.bakeVersion });
        });
}

void RenderView::buildDrawables(const Scene& scene, const Visibility& visibility,
                                const PoseBuffer* poses) {
    PROFILE_SCOPE("RenderView::buildDrawables");

    // Reuse capacity from the previous frame; only grows, never shrinks.
    drawables.clear();
    drawables.reserve(visibility.entries.size());

    for (const auto& entry : visibility.entries) {
        // deleted between cull and render
        if (!scene.isAlive(entry.id)) continue;
        const Mesh& mesh = scene.get<Mesh>(entry.id);
        // unresolved slot
        if (!entry.mesh || !mesh.material) continue;

        DrawableData drawable;
        // The level the cull selected, not the component's own handle.
        drawable.mesh         = entry.mesh;
        drawable.material     = mesh.material;
        drawable.model        = entry.model;
        // Inverse-transpose once per drawable here, not per vertex in two shaders.
        drawable.normalMatrix = glm::transpose(glm::inverse(glm::mat3(entry.model)));
        drawable.worldMin     = entry.worldMin;
        drawable.worldMax     = entry.worldMax;
        drawable.castShadows  = mesh.castShadows;
        appendPose(poses, entry.id, skinMatrices, drawable.skinFirst, drawable.skinCount);
        drawables.push_back(drawable);
    }
}

void RenderView::buildShadowCasters(const Scene& scene, const Visibility& visibility,
                                    const PoseBuffer* poses) {
    PROFILE_SCOPE("RenderView::buildShadowCasters");

    shadowCasters.clear();
    shadowCasters.reserve(visibility.shadowCasters.size());

    // The world AABB is carried so the backend can frustum-cull per light. The
    // model + bounds were already computed by the VisibilitySystem.
    for (const VisibleEntity& entry : visibility.shadowCasters) {
        // deleted between cull and render
        if (!scene.isAlive(entry.id)) continue;
        // unresolved slot
        if (!entry.mesh) continue;

        ShadowCasterData caster;
        caster.mesh    = entry.mesh;
        caster.model   = entry.model;
        caster.aabbMin = entry.worldMin;
        caster.aabbMax = entry.worldMax;
        appendPose(poses, entry.id, skinMatrices, caster.skinFirst, caster.skinCount);
        shadowCasters.push_back(caster);
    }
}

} // namespace Vkm::Engine
