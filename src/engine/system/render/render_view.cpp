#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/render_view.h"

#include <algorithm>

#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/decal.h"
#include "ecs/component/particle_emitter.h"
#include "ecs/component/irradiance_volume.h"
#include "ecs/component/transform.h"
#include "ecs/component/world_transform.h"
#include "ecs/environment.h"
#include "system/visibility/visibility.h"

#include "debug/profiler.h"

namespace Engine {

void RenderView::build(
    const Scene& scene,
    const Visibility& visibility,
    const UIDrawData* uiData
) {
    PROFILE_SCOPE("RenderView::build");

    // The scene's lighting environment.
    environment = scene.environment();

    // The UI overlay is independent of the 3D camera, so snapshot it before the
    // no-camera early-out (a HUD/menu still draws when nothing 3D is in view).
    // Vector assignment reuses the destination capacity.
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
        return;
    }

    buildCamera(visibility);
    buildLights(scene);
    buildProbes(scene);
    buildDecals(scene);
    buildParticles(scene);
    buildIrradianceVolumes(scene);
    buildDrawables(scene, visibility);
    buildShadowCasters(scene, visibility);
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

    // Every enabled light, resolved to world space. Position/rotation come from
    // the WorldTransform when the light is parented, else its local Transform.
    scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& transform) {
        if (!light.enabled) return;

        glm::vec3 position;
        glm::quat rotation;
        if (scene.has<WorldTransform>(id)) {
            const glm::mat4& world = scene.get<WorldTransform>(id).model;
            position = glm::vec3(world[3]);
            rotation = Math::worldRotationOf(world);
        } else {
            position = transform.position;
            rotation = transform.rotation;
        }

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

        // Area lights: fold rotation + size into world-space half-extent
        // axes. Disk uses areaRadius on both so cross(U, V) stays a clean
        // surface normal.
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

    // Every reflection probe, resolved to world space. Position comes from the
    // WorldTransform when the probe is parented, else its local Transform.
    scene.forEach<ReflectionProbe, Transform>(
        [&](EntityId id, const ReflectionProbe& probe, const Transform& transform) {
            glm::vec3 position = transform.position;
            if (scene.has<WorldTransform>(id)) {
                position = glm::vec3(scene.get<WorldTransform>(id).model[3]);
            }
            probes.push_back({ position, probe.halfExtents, probe.falloff, probe.intensity,
                               probe.resolution, probe.bakeVersion });
        });
}

void RenderView::buildDecals(const Scene& scene) {
    decals.clear();

    // Every decal, resolved to world space. The box transform comes from the
    // WorldTransform when the decal is parented, else its local Transform.
    scene.forEach<Decal, Transform>(
        [&](EntityId id, const Decal& decal, const Transform& transform) {
            glm::mat4 model = Transform::computeModelMatrix(transform);
            if (scene.has<WorldTransform>(id)) {
                model = scene.get<WorldTransform>(id).model;
            }
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
            glm::vec3 center = transform.position;
            if (scene.has<WorldTransform>(id)) {
                center = glm::vec3(scene.get<WorldTransform>(id).model[3]);
            }
            irradianceVolumes.push_back({ center, v.halfExtents,
                                          v.resolutionX, v.resolutionY, v.resolutionZ,
                                          v.intensity, v.bakeVersion });
        });
}

void RenderView::buildDrawables(const Scene& scene, const Visibility& visibility) {
    PROFILE_SCOPE("RenderView::buildDrawables");

    // Reuse capacity from the previous frame; only grows, never shrinks.
    drawables.clear();
    drawables.reserve(visibility.entries.size());

    // One drawable per visible entity. Just snapshot handles + matrix - the
    // backend resolves the handles and decides how to sort / batch them.
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
        drawable.castShadows  = mesh.castShadows;
        drawables.push_back(drawable);
    }
}

void RenderView::buildShadowCasters(const Scene& scene, const Visibility& visibility) {
    PROFILE_SCOPE("RenderView::buildShadowCasters");

    shadowCasters.clear();
    shadowCasters.reserve(visibility.shadowCasters.size());

    // The caster set is scene-wide (not camera-culled); resolve each entity's
    // mesh handle and carry its world AABB so the backend can frustum-cull per
    // light. The model + bounds were already computed by the VisibilitySystem.
    for (const VisibleEntity& entry : visibility.shadowCasters) {
        // deleted between cull and render
        if (!scene.isAlive(entry.id)) continue;
        // unresolved slot
        if (!entry.mesh) continue;

        shadowCasters.push_back({ entry.mesh, entry.model, entry.worldMin, entry.worldMax });
    }
}

} // namespace Engine
