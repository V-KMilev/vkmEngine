#include "system/render/render_view.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vkm_log/logger.h"

#include "base/math/aabb.h"
#include "base/math/frustum.h"
#include "base/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/scene/world_transform.h"
#include "ecs/component/effect/decal.h"
#include "ecs/component/light/irradiance_volume.h"
#include "ecs/component/light/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/outline.h"
#include "ecs/component/effect/particle_emitter.h"
#include "ecs/component/light/reflection_probe.h"
#include "ecs/settings/environment.h"
#include "core/frame/pose_buffer.h"
#include "core/frame/live_particles.h"
#include "core/frame/visibility.h"

#include "base/profile/profiler.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Snapshot every enabled light into world space.
 *
 * @param view  The view being refilled.
 * @param scene Scene whose Light components are gathered.
 */
void buildLights(RenderView& view, const Scene& scene) {
    PROFILE_SCOPE("RenderView::buildLights");
    view.lists.lights.clear();
    view.lists.lights.reserve(scene.count<Light>());
    view.lists.keyIndex.reset();

    const EntityId key = findKeyLight(scene);
    scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& transform) {
        if (!light.enabled) return;

        const glm::vec3 position = resolvedWorldPosition(scene, id, transform);
        const glm::quat rotation = resolvedWorldRotation(scene, id, transform);

        LightData data{};
        data.type       = light.type;
        data.entitySlot = id.slot();
        data.color      = light.color;
        data.intensity  = light.intensity;
        data.wave       = flickerWave(light, view.worldSeconds, id.slot());
        data.position   = position;
        data.direction  = Math::computeForward(rotation);

        data.radius         = light.radius;
        data.innerConeAngle = light.innerConeAngle;
        data.outerConeAngle = light.outerConeAngle;

        data.inCaptures       = light.inCaptures;
        data.movable          = light.movable;
        data.castShadows      = light.castShadows;
        data.shadowDistance   = light.shadowDistance;
        data.shadowPriority   = light.shadowPriority;
        // Held to 45 degrees of radius: the tangent the disc is sized by diverges at 90.
        data.sourceRadius = light.type == LightType::Directional
            ? std::clamp(light.sourceRadius, 0.0f, glm::quarter_pi<float>())
            : std::max(light.sourceRadius, 0.0f);

        if (id == key) view.lists.keyIndex = static_cast<uint32_t>(view.lists.lights.size());
        view.lists.lights.push_back(data);
    });
}

/**
 * @brief Snapshot every reflection probe into world space.
 *
 * @param view  The view being refilled.
 * @param scene Scene whose ReflectionProbe components are gathered.
 */
void buildProbes(RenderView& view, const Scene& scene) {
    view.lists.probes.clear();

    scene.forEach<ReflectionProbe, Transform>(
        [&](EntityId id, const ReflectionProbe& probe, const Transform& transform) {
            const glm::vec3 position = resolvedWorldPosition(scene, id, transform);
            view.lists.probes.push_back({
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
 * @brief Snapshot every decal the camera sees into world space (box transform + its inverse), a
 *        material's together.
 *
 * @param view  The view being refilled, its camera set.
 * @param scene Scene whose Decal components are gathered.
 */
void buildDecals(RenderView& view, const Scene& scene) {
    PROFILE_SCOPE("RenderView::buildDecals");
    std::vector<DecalData>& decals = view.lists.decals;
    decals.clear();

    const Math::Frustum seen = Math::extractFrustum(view.camera.viewProjection);
    const Math::AABB    box{glm::vec3(-0.5f), glm::vec3(0.5f)};
    scene.forEach<Decal, Transform>([&](EntityId id, const Decal& decal, const Transform& transform) {
        if (!decal.enabled) return;

        const glm::mat4 model = resolvedWorldMatrix(scene, id, transform);
        if (!Math::frustumIntersectsAABB(seen, Math::transform(model, box))) return;
        decals.push_back({model, glm::inverse(model), decal.material, decal.angleFade, decal.opacity});
    });
    // Stable, so decals of one material still overlap in the order the scene holds them.
    std::stable_sort(decals.begin(), decals.end(), [](const DecalData& a, const DecalData& b) {
        return a.material.id() < b.material.id();
    });
}

/**
 * @brief Snapshot every enabled Outline whose mesh the eye sees.
 *
 * @param view       The view being refilled.
 * @param scene      Scene whose Outline components are gathered.
 * @param visibility The cull, whose objects are the meshes in their storage's order.
 */
void buildOutlines(RenderView& view, const Scene& scene, const Visibility& visibility) {
    view.lists.outlines.clear();
    const SparseSet<Mesh>* meshes = scene.storage<Mesh>();
    if (!meshes || scene.count<Outline>() == 0) return;

    PROFILE_SCOPE("RenderView::buildOutlines");
    const std::vector<uint32_t>& seen = visibility.objects.visible;
    scene.forEach<Outline>([&](EntityId id, const Outline& outline) {
        if (!outline.enabled || !meshes->contains(id.slot())) return;
        // An object is its Mesh's place in storage; the eye's list holds them in that order.
        const uint32_t object = meshes->indexOf(id.slot());
        if (object >= visibility.entities.size() || visibility.entities[object] != id) return;
        if (!std::binary_search(seen.begin(), seen.end(), object)) return;
        view.lists.outlines.push_back({object, outline.color, outline.width, outline.throughWalls});
    });
}

/// How far through its life @p p is: 0 at birth to 1 at its end, and 1 for one with no lifetime.
float lifeOf(const Particle& p) { return p.lifetime > 0.0f ? p.age / p.lifetime : 1.0f; }

/**
 * @brief Add the light @p emitter's live particles give off together (ParticleEmitter::lightIntensity).
 *
 * At their middle, weighted by each one's alpha, as large as they are spread, which softens its
 * highlight: the whole fades and dies as they do.
 *
 * @param view    The view being refilled.
 * @param id      The emitter's entity.
 * @param emitter Its light.
 * @param live    Its particles.
 */
void addParticleLight(
    RenderView& view,
    EntityId id,
    const ParticleEmitter& emitter,
    const std::vector<Particle>& live
) {
    const auto alphaOf = [&](const Particle& p) { return std::max(emitter.color.at(lifeOf(p)).a, 0.0f); };
    float shine = 0.0f;
    glm::vec3 middle(0.0f);
    for (const Particle& p : live) {
        shine  += alphaOf(p);
        middle += p.position * alphaOf(p);
    }
    if (shine <= 0.0f) return;
    middle /= shine;
    float spread = 0.0f;
    for (const Particle& p : live) {
        const glm::vec3 off = p.position - middle;
        spread += glm::dot(off, off) * alphaOf(p);
    }

    LightData light{};
    light.type         = LightType::Point;
    light.entitySlot   = id.slot();
    light.color        = emitter.lightColor;
    light.intensity    = emitter.lightIntensity * shine;
    light.position     = middle;
    light.direction    = glm::vec3(0.0f, -1.0f, 0.0f);
    light.radius       = std::max(emitter.lightRadius, 0.01f);
    light.sourceRadius = std::min(std::sqrt(spread / shine), 0.5f * light.radius);
    light.inCaptures   = false;
    view.lists.lights.push_back(light);
}

/// The box @p items from @p first on can cover, ParticleData or StripPoint, each by its reachOf.
template <typename Item>
Math::AABB boxOf(const std::vector<Item>& items, size_t first) {
    Math::AABB box = Math::AABB::empty();
    for (size_t i = first; i < items.size(); ++i) box.expand(Item::reachOf(items[i]));
    return box;
}

/**
 * @brief Flatten every emitter's live particles into the frame's billboards and strips, each list
 *        by its blend, unsorted.
 *
 * An emitter whose particles all lie outside the eye's view is left out, so a frame that sees none
 * draws none; one on the viewmodel layer is drawn through another projection, and is kept.
 *
 * @param view         The view being refilled.
 * @param scene        Scene whose ParticleEmitter components are gathered.
 * @param particles    The live particles of each, or null.
 * @param throughAnEye Whether a scene camera sees the frame, the one ParticleEmitter::seenBy names.
 */
void buildParticles(RenderView& view, const Scene& scene, const LiveParticles* particles, bool throughAnEye) {
    PROFILE_SCOPE("RenderView::buildParticles");
    view.lists.particlesAdditive.clear();
    view.lists.particlesAlpha.clear();
    view.lists.particlesDistortion.clear();
    view.lists.stripPoints.clear();
    view.lists.stripsAdditive.clear();
    view.lists.stripsAlpha.clear();
    view.lists.stripsDistortion.clear();
    view.lists.particleTextures.clear();

    const Math::Frustum seen = Math::extractFrustum(view.camera.viewProjection);
    scene.forEach<ParticleEmitter>([&](EntityId id, const ParticleEmitter& emitter) {
        const LiveParticles::Pool* pool = particles ? particles->poolOf(id) : nullptr;
        // Lighting what is round it whoever draws the particles: a muzzle flash lights the wall.
        if (pool && emitter.lightIntensity > 0.0f) addParticleLight(view, id, emitter, pool->particles);
        const bool hidden = throughAnEye && emitter.seenBy == SeenBy::AllButEye;
        if (!pool || pool->particles.empty() || hidden) return;
        const bool viewmodel = throughAnEye && emitter.seenBy == SeenBy::Viewmodel;

        // A few emitters a frame, so a walk finds a texture's place.
        const auto placeOf = [&](const TextureHandle& handle) {
            if (!handle) return 0.0f;
            auto& textures    = view.lists.particleTextures;
            const auto found  = std::find(textures.begin(), textures.end(), handle);
            const float place = static_cast<float>(found - textures.begin()) + 1.0f;
            if (found == textures.end()) textures.push_back(handle);
            return place;
        };
        const float texture   = placeOf(emitter.texture);
        const float normalMap = emitter.blend == ParticleBlend::Alpha ? placeOf(emitter.normalMap) : 0.0f;
        const float extra     = emitter.blend == ParticleBlend::Alpha ? emitter.emission
            : emitter.blend == ParticleBlend::Distortion           ? emitter.distortion
            : 0.0f;

        // What a particle looks like at its age: its size and its color, intensity in the RGB.
        const auto sizeOf = [&](const Particle& p) { return p.size * emitter.sizeOverLife.at(lifeOf(p)); };
        const auto colorOf = [&](const Particle& p) {
            const glm::vec4 c = emitter.color.at(lifeOf(p));
            return glm::vec4(glm::vec3(c) * std::max(emitter.intensity, 0.0f), c.a);
        };

        if (emitter.render == ParticleRender::Trail) {
            auto& strips = emitter.blend == ParticleBlend::Additive ? view.lists.stripsAdditive
                : emitter.blend == ParticleBlend::Alpha             ? view.lists.stripsAlpha
                : view.lists.stripsDistortion;
            const glm::vec4 look(emitter.softness, extra, viewmodel ? 1.0f : 0.0f, 0.0f);
            const auto open = [&]() {
                ParticleStrip strip;
                strip.first   = static_cast<uint32_t>(view.lists.stripPoints.size());
                strip.texture = texture;
                return strip;
            };
            // A point on the one before it has no direction to widen across: it is left out.
            const auto add = [&](const ParticleStrip& strip, glm::vec4 point, glm::vec4 color, float along) {
                auto& points  = view.lists.stripPoints;
                const bool on = points.size() > strip.first
                    && glm::length(glm::vec3(points.back().positionWidth) - glm::vec3(point)) < 1e-5f;
                if (!on) points.push_back({point, color, glm::vec4(along, 0.0f, 0.0f, 0.0f), look});
            };
            // Each point is told its strip's run, which a strip of one point does not have.
            const auto close = [&](ParticleStrip strip) {
                strip.count = static_cast<uint32_t>(view.lists.stripPoints.size()) - strip.first;
                if (strip.count < 2) {
                    view.lists.stripPoints.resize(strip.first);
                    return;
                }
                const float last = static_cast<float>(strip.first + strip.count - 1);
                for (uint32_t i = strip.first; i < strip.first + strip.count; ++i) {
                    view.lists.stripPoints[i].params.y = static_cast<float>(strip.first);
                    view.lists.stripPoints[i].params.z = last;
                }
                strips.push_back(strip);
            };

            const std::vector<Particle>& live = pool->particles;
            // Each particle draws its own trail, narrowing and fading to where it was. A pool not
            // yet stepped since it turned to trails holds none.
            if (pool->trails.size() != live.size() * LiveParticles::TRAIL_POINTS) return;
            const size_t firstPoint = view.lists.stripPoints.size();
            const size_t firstStrip = strips.size();
            for (size_t i = 0; i < live.size(); ++i) {
                const Particle& p = live[i];
                const glm::vec3* points = pool->trailOf(i);
                const float count = static_cast<float>(p.trailCount);
                const float width = sizeOf(p);
                const glm::vec4 color = colorOf(p);
                ParticleStrip strip = open();
                add(strip, glm::vec4(p.position, width), color, 0.0f);
                for (uint32_t k = 0; k < p.trailCount; ++k) {
                    const float along = (static_cast<float>(k) + 1.0f) / count;
                    const float left  = 1.0f - along;
                    add(strip, glm::vec4(points[k], width * left), {glm::vec3(color), color.a * left}, along);
                }
                close(strip);
            }
            if (!viewmodel && !Math::frustumIntersectsAABB(seen, boxOf(view.lists.stripPoints, firstPoint))) {
                view.lists.stripPoints.resize(firstPoint);
                strips.resize(firstStrip);
            }
            return;
        }

        auto& out = emitter.blend == ParticleBlend::Additive ? view.lists.particlesAdditive
            : emitter.blend == ParticleBlend::Alpha          ? view.lists.particlesAlpha
            : view.lists.particlesDistortion;
        const uint32_t columns = sheetSide(emitter.sheetColumns);
        const uint32_t rows    = sheetSide(emitter.sheetRows);
        const glm::vec2 frameSize(1.0f / static_cast<float>(columns), 1.0f / static_cast<float>(rows));
        const bool stretched = emitter.render == ParticleRender::Stretched;
        const float layer    = viewmodel ? 1.0f : 0.0f;

        // The sheet's top row first; an image loads bottom row first (decoded_image.cpp).
        const auto cornerOf = [&](uint32_t frame) {
            return glm::vec2(
                static_cast<float>(frame % columns) * frameSize.x,
                1.0f - static_cast<float>(frame / columns + 1) * frameSize.y
            );
        };
        const size_t first = out.size();
        for (const Particle& p : pool->particles) {
            const SheetFrame sheet = sheetFrameAt(emitter, p.age, p.lifetime, p.frame);
            // A streak lies along its trail, turned by nothing.
            const float angle = stretched ? 0.0f : p.angle + p.spin * p.age;
            const glm::vec3 streak = stretched ? -p.velocity * std::max(emitter.stretch, 0.0f) : glm::vec3();
            out.push_back({
                glm::vec4(p.position, sizeOf(p)),
                colorOf(p),
                glm::vec4(emitter.softness, angle, texture, layer),
                glm::vec4(cornerOf(sheet.frame), frameSize),
                glm::vec4(streak, extra),
                glm::vec4(cornerOf(sheet.next), sheet.blend, normalMap),
            });
        }
        if (!viewmodel && !Math::frustumIntersectsAABB(seen, boxOf(out, first))) out.resize(first);
    });
}

/**
 * @brief Snapshot the scene's irradiance volume (findIrradianceVolume's pick) into world space.
 *
 * @param view  The view being refilled.
 * @param scene Scene to take the volume from.
 */
void buildIrradianceVolume(RenderView& view, const Scene& scene) {
    view.lists.hasIrradianceVolume = false;

    const EntityId id = findIrradianceVolume(scene);
    if (!id) return;

    const IrradianceVolume& volume = scene.get<IrradianceVolume>(id);
    const Transform& transform     = scene.get<Transform>(id);

    // Clamped: the backend sizes the volume's textures from these.
    const glm::uvec3 res = IrradianceVolume::clampedResolution(volume);
    view.lists.irradianceVolume = IrradianceVolumeData{
        resolvedWorldPosition(scene, id, transform),
        volume.halfExtents,
        res.x,
        res.y,
        res.z,
        volume.intensity,
        volume.blendDistance,
        volume.bakeVersion
    };
    view.lists.hasIrradianceVolume = true;
}

} // namespace

void RenderView::build(
    const Scene& scene,
    const Visibility& visibility,
    const UIDrawData* uiData,
    const UIDrawData* coverData,
    const SplashFrame* splashFrame,
    const PoseBuffer* poses,
    const LiveParticles* particles
) {
    PROFILE_SCOPE("RenderView::build");

    // Before the no-camera early-out: a world replaced while unrendered must
    // still reach downstream caches.
    environment = &scene.environment();
    worldEpoch  = scene.epoch();

    // UI, cover and splash draw without a camera, so they precede the early-out too.
    ui     = uiData;
    cover  = coverData;
    splash = splashFrame;

    // The cull publishes empty lists without a camera, so this is never stale.
    objects = &visibility.objects;

    if (!visibility.hasCamera) {
        // An empty snapshot, not a stale one. The camera stands: passes read it, and zeroed is singular.
        hasCamera = false;
        lists     = {};
        return;
    }

    hasCamera = true;
    camera    = visibility.camera;
    buildLights(*this, scene);
    buildProbes(*this, scene);
    buildDecals(*this, scene);
    buildOutlines(*this, scene, visibility);
    buildParticles(*this, scene, particles, static_cast<bool>(visibility.cameraEntity));
    buildIrradianceVolume(*this, scene);
    lists.waves = scene.water().waves();

    // A buffer holding no slices posed nothing, so it is passed on as null.
    lists.skinMatrices = (poses && !poses->slices().empty()) ? &poses->palette() : nullptr;
}

} // namespace Vkm::Engine
