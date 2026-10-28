#include "system/render/render_view.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/math/bounds.h"
#include "core/math/frustum.h"
#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/planar_reflector.h"
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
        data.wave       = flickerWave(light, view.worldSeconds, id.slot());
        data.position   = position;
        data.direction  = Math::computeForward(rotation);

        data.radius         = light.radius;
        data.innerConeAngle = light.innerConeAngle;
        data.outerConeAngle = light.outerConeAngle;

        data.inCaptures       = light.inCaptures;
        data.movable          = light.movable;
        data.castShadows      = light.castShadows;
        data.shadowBias       = light.shadowBias;
        data.shadowNormalBias = light.shadowNormalBias;
        data.shadowDistance   = light.shadowDistance;
        data.shadowPriority   = light.shadowPriority;
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
 * @brief Snapshot every enabled planar reflector into world space.
 *
 * @param view  The view being refilled.
 * @param scene Scene whose PlanarReflector components are gathered.
 */
void buildPlanarReflectors(RenderView& view, const Scene& scene) {
    view.planarReflectors.clear();

    scene.forEach<PlanarReflector, Transform>(
        [&](EntityId id, const PlanarReflector& reflector, const Transform& transform) {
            if (!reflector.enabled || reflector.halfExtents.x <= 0.0f || reflector.halfExtents.y <= 0.0f) {
                return;
            }
            const glm::mat4 model = resolvedWorldMatrix(scene, id, transform);
            const glm::vec3 up(model[1]);
            const glm::vec3 across(model[0]);
            if (glm::length(up) < 1e-6f || glm::length(across) < 1e-6f) return;
            const glm::vec3 normal = glm::normalize(up);
            const glm::vec3 axisU  = glm::normalize(across - normal * glm::dot(across, normal));
            view.planarReflectors.push_back({
                glm::vec3(model[3]),
                normal,
                axisU,
                glm::cross(axisU, normal),
                reflector.halfExtents,
                glm::clamp(
                    reflector.renderScale,
                    PlanarReflector::MIN_RENDER_SCALE,
                    PlanarReflector::MAX_RENDER_SCALE
                ),
                std::max(reflector.drawDistance, 0.0f)
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
    view.decals.clear();

    const Math::Frustum seen = Math::extractFrustum(view.camera.viewProjection);
    const Math::AABB    box{glm::vec3(-0.5f), glm::vec3(0.5f)};
    scene.forEach<Decal, Transform>([&](EntityId id, const Decal& decal, const Transform& transform) {
        if (!decal.enabled) return;

        const glm::mat4 model = resolvedWorldMatrix(scene, id, transform);
        if (!Math::frustumIntersectsAABB(seen, Math::transform(model, box))) return;
        view.decals.push_back({model, glm::inverse(model), decal.material, decal.angleFade, decal.opacity});
    });
    // Stable, so decals of one material still overlap in the order the scene holds them.
    std::stable_sort(view.decals.begin(), view.decals.end(), [](const DecalData& a, const DecalData& b) {
        return a.material.id() < b.material.id();
    });
}

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
    const auto alphaOf = [&](const Particle& p) {
        const float life = p.lifetime > 0.0f ? p.age / p.lifetime : 1.0f;
        return std::max(emitter.color.at(life).a, 0.0f);
    };
    float     shine = 0.0f;
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
    view.lights.push_back(light);
}

/**
 * @brief Flatten every emitter's live particles into the frame's billboards and strips, each list
 *        by its blend, unsorted.
 *
 * @param view         The view being refilled.
 * @param scene        Scene whose ParticleEmitter components are gathered.
 * @param particles    The live particles of each, or null.
 * @param throughAnEye Whether a scene camera sees the frame, the one ParticleEmitter::seenBy names.
 */
void buildParticles(RenderView& view, const Scene& scene, const LiveParticles* particles, bool throughAnEye) {
    PROFILE_SCOPE("RenderView::buildParticles");
    view.particlesAdditive.clear();
    view.particlesAlpha.clear();
    view.particlesDistortion.clear();
    view.stripPoints.clear();
    view.stripsAdditive.clear();
    view.stripsAlpha.clear();
    view.stripsDistortion.clear();
    view.particleTextures.clear();

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
            auto& textures = view.particleTextures;
            const auto found = std::find(textures.begin(), textures.end(), handle);
            const float place = static_cast<float>(found - textures.begin()) + 1.0f;
            if (found == textures.end()) textures.push_back(handle);
            return place;
        };
        const float texture   = placeOf(emitter.texture);
        const float normalMap = emitter.blend == ParticleBlend::Alpha ? placeOf(emitter.normalMap) : 0.0f;
        const float extra = emitter.blend == ParticleBlend::Alpha ? emitter.emission
            : emitter.blend == ParticleBlend::Distortion           ? emitter.distortion
            : 0.0f;

        // What a particle looks like at its age: its size and its color, intensity in the RGB.
        const auto lifeOf = [](const Particle& p) { return p.lifetime > 0.0f ? p.age / p.lifetime : 1.0f; };
        const auto sizeOf = [&](const Particle& p) { return p.size * emitter.sizeOverLife.at(lifeOf(p)); };
        const auto colorOf = [&](const Particle& p) {
            const glm::vec4 c = emitter.color.at(lifeOf(p));
            return glm::vec4(glm::vec3(c) * std::max(emitter.intensity, 0.0f), c.a);
        };

        if (emitter.render == ParticleRender::Ribbon || emitter.render == ParticleRender::Trail) {
            auto& strips = emitter.blend == ParticleBlend::Additive ? view.stripsAdditive
                : emitter.blend == ParticleBlend::Alpha             ? view.stripsAlpha
                : view.stripsDistortion;
            const glm::vec4 look(emitter.softness, extra, viewmodel ? 1.0f : 0.0f, 0.0f);
            const auto open = [&]() {
                ParticleStrip strip;
                strip.first   = static_cast<uint32_t>(view.stripPoints.size());
                strip.texture = texture;
                return strip;
            };
            // A point on the one before it has no direction to widen across: it is left out.
            const auto add = [&](const ParticleStrip& strip, glm::vec4 point, glm::vec4 color, float along) {
                auto& points = view.stripPoints;
                const bool on = points.size() > strip.first
                    && glm::length(glm::vec3(points.back().positionWidth) - glm::vec3(point)) < 1e-5f;
                if (!on) points.push_back({point, color, glm::vec4(along, 0.0f, 0.0f, 0.0f), look});
            };
            // Each point is told its strip's run, which a strip of one point does not have.
            const auto close = [&](ParticleStrip strip) {
                strip.count = static_cast<uint32_t>(view.stripPoints.size()) - strip.first;
                if (strip.count < 2) {
                    view.stripPoints.resize(strip.first);
                    return;
                }
                const float last = static_cast<float>(strip.first + strip.count - 1);
                for (uint32_t i = strip.first; i < strip.first + strip.count; ++i) {
                    view.stripPoints[i].params.y = static_cast<float>(strip.first);
                    view.stripPoints[i].params.z = last;
                }
                strips.push_back(strip);
            };

            const std::vector<Particle>& live = pool->particles;
            if (emitter.render == ParticleRender::Ribbon) {
                // Newest first, so the strip's head is where the emitter is now.
                ParticleStrip strip = open();
                const float last = static_cast<float>(std::max<size_t>(live.size() - 1, 1));
                for (size_t i = live.size(); i-- > 0;) {
                    const Particle& p = live[i];
                    const float along = static_cast<float>(live.size() - 1 - i) / last;
                    add(strip, glm::vec4(p.position, sizeOf(p)), colorOf(p), along);
                }
                close(strip);
                return;
            }
            // Each particle draws its own trail, narrowing and fading to where it was. A pool not
            // yet stepped since it turned to trails holds none.
            if (pool->trails.size() != live.size() * LiveParticles::TRAIL_POINTS) return;
            for (size_t i = 0; i < live.size(); ++i) {
                const Particle&  p      = live[i];
                const glm::vec3* points = pool->trailOf(i);
                const float      count  = static_cast<float>(p.trailCount);
                const float      width  = sizeOf(p);
                const glm::vec4  color  = colorOf(p);
                ParticleStrip strip = open();
                add(strip, glm::vec4(p.position, width), color, 0.0f);
                for (uint32_t k = 0; k < p.trailCount; ++k) {
                    const float along = (static_cast<float>(k) + 1.0f) / count;
                    const float left  = 1.0f - along;
                    add(strip, glm::vec4(points[k], width * left), {glm::vec3(color), color.a * left}, along);
                }
                close(strip);
            }
            return;
        }

        auto& out = emitter.blend == ParticleBlend::Additive ? view.particlesAdditive
            : emitter.blend == ParticleBlend::Alpha          ? view.particlesAlpha
            : view.particlesDistortion;
        const uint32_t  columns   = sheetSide(emitter.sheetColumns);
        const uint32_t  rows      = sheetSide(emitter.sheetRows);
        const glm::vec2 frameSize(1.0f / static_cast<float>(columns), 1.0f / static_cast<float>(rows));
        const bool      stretched = emitter.render == ParticleRender::Stretched;
        const float     layer     = viewmodel ? 1.0f : 0.0f;

        // The sheet's top row first; an image loads bottom row first (image_loaders.cpp).
        const auto cornerOf = [&](uint32_t frame) {
            return glm::vec2(
                static_cast<float>(frame % columns) * frameSize.x,
                1.0f - static_cast<float>(frame / columns + 1) * frameSize.y
            );
        };
        for (const Particle& p : pool->particles) {
            const SheetFrame sheet = sheetFrameAt(emitter, p.age, p.lifetime, p.frame);
            // A streak lies along its trail, turned by nothing.
            const float     angle  = stretched ? 0.0f : p.angle + p.spin * p.age;
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
    const UIDrawData* coverData,
    const SplashFrame* splashFrame,
    const PoseBuffer* poses,
    const LiveParticles* particles,
    const OceanData* sea
) {
    PROFILE_SCOPE("RenderView::build");

    environment = scene.environment();
    // Before the no-camera early-out: a world replaced while unrendered must
    // still reach downstream caches.
    worldEpoch  = scene.epoch();

    // UI, cover and splash draw without a camera, so they precede the early-out too.
    ui    = uiData;
    cover = coverData;

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
            seconds,
            bakeSeconds,
            worldClock,
            arriving,
            cameraData,
            cameraPresent,
            objectLists,
            lightList,
            probeList,
            planarList,
            decalList,
            particlesAdd,
            particlesBlend,
            particlesBend,
            stripPointList,
            stripsAdd,
            stripsBlend,
            stripsBend,
            particleTextureList,
            volume,
            volumePresent,
            seaState,
            palettes,
            debugLines,
            renderSettings,
            env,
            uiOverlay,
            splashState,
            coverList,
            coverShown,
            coverIn,
            coverFilled,
            coverWater,
            pointerAt,
            epoch
        ] = *this;

        cameraPresent = false;
        lightList.clear();
        probeList.clear();
        planarList.clear();
        decalList.clear();
        particlesAdd.clear();
        particlesBlend.clear();
        particlesBend.clear();
        stripPointList.clear();
        stripsAdd.clear();
        stripsBlend.clear();
        stripsBend.clear();
        particleTextureList.clear();
        volumePresent = false;
        seaState.present = false;
        palettes      = nullptr;
        debugLines    = DebugFrame{};
        return;
    }

    hasCamera = true;
    camera = visibility.camera;
    buildLights(*this, scene);
    buildProbes(*this, scene);
    buildPlanarReflectors(*this, scene);
    buildDecals(*this, scene);
    buildParticles(*this, scene, particles, static_cast<bool>(visibility.cameraEntity));
    buildIrradianceVolume(*this, scene);
    ocean = sea ? *sea : OceanData{};

    // A buffer holding no slices posed nothing, so it is passed on as null.
    skinMatrices = (poses && !poses->slices().empty()) ? &poses->palette() : nullptr;
}

} // namespace Vkm::Engine
