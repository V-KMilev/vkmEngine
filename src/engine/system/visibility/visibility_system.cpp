#define VKM_LOG_CATEGORY "VISIBILITY"

#include "system/visibility/visibility_system.h"

#include <algorithm>
#include <atomic>

#include <glm/gtc/constants.hpp>

#include "logger.h"

#include "core/clock.h"
#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"
#include "platform/window/window_manager.h"

#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"

#include "core/host_chrome.h"
#include "core/math/bounds.h"
#include "system/animation/pose_buffer.h"
#include "system/ocean/ocean_data.h"
#include "system/render/render_settings.h"
#include "system/visibility/culling.h"
#include "system/visibility/host_view.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief The box this frame's pose actually occupies, in the mesh's own space.
 *
 * The bind-pose box under-sizes a posed character, and an under-sized box culls
 * visible geometry. The posed bone-origin box is padded by `skinRadius` times
 * the largest bone scale; measured in mesh space, applied in rig space: exact
 * for a rigid bind transform, else conservative, as `maxBoneScale` is floored at 1.
 *
 * @param mesh Mesh being bounded.
 * @param slice The mesh's rig slice this frame, or null when nothing poses it.
 * @param outMin Filled with the low corner of the local-space box.
 * @param outMax Filled with the high corner.
 */
void poseLocalBounds(const MeshAsset& mesh, const PoseSlice* slice, glm::vec3& outMin, glm::vec3& outMax) {
    outMin = mesh.boundsMin;
    outMax = mesh.boundsMax;
    if (!slice || slice->count == 0) return;

    const glm::vec3 pad(mesh.skinRadius * slice->maxBoneScale);
    outMin = slice->originMin - pad;
    outMax = slice->originMax + pad;
}

// What the cull records about each object, for the serial gather.
constexpr uint8_t STATE_DRAWN   = 1u << 0;  ///< Goes into the scene-wide list.
constexpr uint8_t STATE_VISIBLE = 1u << 1;  ///< The camera sees it.
constexpr uint8_t STATE_CASTS   = 1u << 2;  ///< Goes into the scene list's caster prefix.
constexpr uint8_t STATE_CHANGED = 1u << 3;  ///< Its columns or its lists differ from the last frame's.

// The bits that put an object in the scene-wide list and its caster prefix.
constexpr uint8_t STATE_LISTED = STATE_DRAWN | STATE_CASTS;

/**
 * @brief Pick the geometry for this entity at this distance.
 *
 * Distance is to the bounds centre, not the origin, so an object whose pivot
 * sits far from its body does not pop.
 *
 * @tparam LODStorage The LOD component's storage.
 * @param mesh       Its handle is the full-detail level, and the fallback.
 * @param lodStorage Null when the scene has no LOD components.
 * @param entityIdx  The entity's slot, which keys its LOD.
 * @param worldMin   World bounds' low corner.
 * @param worldMax   World bounds' high corner.
 * @param context    Supplies the camera position and LOD distance scale.
 * @return The chosen mesh; never empty when the Mesh component had one.
 */
template <typename LODStorage>
MeshHandle selectLOD(
    const Mesh& mesh,
    const LODStorage* lodStorage,
    uint32_t entityIdx,
    const glm::vec3& worldMin,
    const glm::vec3& worldMax,
    const VisibilityContext& context
) {
    if (!lodStorage || !lodStorage->contains(entityIdx)) return mesh.mesh;

    const LOD& lod = lodStorage->get(entityIdx);
    if (lod.levels.empty()) return mesh.mesh;

    const glm::vec3 centre = (worldMin + worldMax) * 0.5f;
    const glm::vec3 delta  = centre - context.cameraPosition;
    const float distance   = glm::length(delta);
    const float bias       = glm::max(lod.bias, glm::epsilon<float>());
    const float scaled     = distance * context.lodDistanceScale / bias;

    for (const LODLevel& level : lod.levels) {
        if (scaled <= level.maxDistance) return level.mesh ? level.mesh : mesh.mesh;
    }

    // Past the last threshold the coarsest level keeps drawing; culling is Culling::isNearEnough's call.
    const MeshHandle& last = lod.levels.back().mesh;
    return last ? last : mesh.mesh;
}

// What the cull keeps about a fixed mesh between frames (VisibilitySystem::m_fixed).
constexpr uint8_t FIXED_KEPT       = 1u << 0;  ///< Its columns are kept; zero is "work it out".
constexpr uint8_t FIXED_DRAWN      = 1u << 1;  ///< As STATE_DRAWN.
constexpr uint8_t FIXED_CASTS      = 1u << 2;  ///< As STATE_CASTS.
constexpr uint8_t FIXED_NOT_EYE    = 1u << 3;  ///< Seen by all but the eye: never visible.
constexpr uint8_t FIXED_LOD_SELECT = 1u << 4;  ///< An LOD component picks its mesh each frame.
constexpr uint8_t FIXED_WORLD      = 1u << 5;  ///< Its matrix is its WorldTransform's.

/**
 * @brief What working out one object reads, beyond its index.
 */
struct ObjectInputs {
    const Scene&                     scene;
    const ResourceManager&           resources;
    const SparseSet<Mesh>&           meshes;
    const SparseSet<Transform>&      transforms;
    const SparseSet<WorldTransform>* worldTransforms;  ///< Null when no entity ever had one.
    const SparseSet<LOD>*            lods;             ///< Null when no entity ever had one.
    const PoseBuffer*                poses;            ///< Null when nothing is posed.
    const VisibilityContext&         context;
    bool                             sea;              ///< The scene has an ocean.
    glm::vec2                        swell;            ///< How far it moves water, across and up.
    bool                             throughAnEye;     ///< The frame is seen through a scene camera.
};

/**
 * @brief One object's columns of RenderObjects, and what the cull records about it.
 */
struct ObjectColumns {
    glm::mat4  model{1.0f};
    Math::AABB bounds;
    ObjectDraw draw;
    uint32_t   skinFirst = 0;
    EntityId   entity{};
    uint8_t    state      = 0;      ///< STATE_DRAWN and STATE_CASTS.
    bool       notTheEye  = false;  ///< Seen by all but the eye, and the frame is the eye's.
    uint8_t    fixedState = 0;      ///< What m_fixed keeps for it; zero unless it is fixed.
};

/**
 * @brief Work out object @p index's columns from its components and assets.
 *
 * @param in    What it is worked out from.
 * @param index Its place in the Mesh storage.
 * @param out   Filled when it draws.
 * @return Whether it draws at all: shown, with a mesh and material, a Transform and a valid box.
 */
bool computeObject(const ObjectInputs& in, uint32_t index, ObjectColumns& out) {
    const uint32_t entityIdx = in.meshes.keyAt(index);
    const Mesh&    mesh      = in.meshes.dataAt(index);

    if (!mesh.visible) return false;
    // Nothing to shade it with draws and casts nothing, decided once for every list.
    if (!mesh.mesh || !mesh.material) return false;
    if (!in.transforms.contains(entityIdx)) return false;

    const auto& meshAsset = in.resources.get(mesh.mesh);
    if (!meshAsset.bounds().valid()) return false;

    const bool hasWorld = in.worldTransforms && in.worldTransforms->contains(entityIdx);
    out.model = hasWorld
        ? in.worldTransforms->get(entityIdx).model
        : Transform::computeModelMatrix(in.transforms.get(entityIdx));

    // The pose sizes a skinned mesh's box; an unskinned one keeps its bind bounds.
    const bool skinned = !meshAsset.skin.empty();
    const EntityId   entity = in.scene.entityAt(entityIdx);
    const PoseSlice* slice  = (in.poses && skinned) ? in.poses->sliceOf(entity) : nullptr;
    glm::vec3 localMin, localMax;
    poseLocalBounds(meshAsset, slice, localMin, localMax);

    out.bounds = Math::transform(out.model, {localMin, localMax});
    const MaterialAsset* surface = in.sea ? in.resources.tryGet(mesh.material) : nullptr;
    const bool           waves   = in.sea && surface && surface->waves;
    if (waves) {
        out.bounds.min -= glm::vec3(in.swell.x, in.swell.y, in.swell.x);
        out.bounds.max += glm::vec3(in.swell.x, in.swell.y, in.swell.x);
    }

    const bool viewmodel = in.throughAnEye && mesh.seenBy == SeenBy::Viewmodel;
    // Kept between frames: what does not move, is not skinned, rides no sea and is no held weapon.
    const bool fixed = !mesh.movable && !skinned && !waves && !viewmodel;
    out.draw = ObjectDraw{
        selectLOD(mesh, in.lods, entityIdx, out.bounds.min, out.bounds.max, in.context),
        mesh.material,
        slice ? slice->count : 0u,
        waves,
        viewmodel,
        fixed
    };
    out.skinFirst = slice ? slice->first : 0u;
    out.entity    = entity;

    const bool eyeOnly = viewmodel || (in.throughAnEye && mesh.seenBy == SeenBy::EyeOnly);
    out.notTheEye = in.throughAnEye && mesh.seenBy == SeenBy::AllButEye;
    out.state     = eyeOnly ? 0 : STATE_DRAWN;
    if (mesh.castShadows && !waves && !eyeOnly) out.state |= STATE_CASTS;

    out.fixedState = 0;
    if (fixed) {
        out.fixedState = FIXED_KEPT;
        if (out.state & STATE_DRAWN) out.fixedState |= FIXED_DRAWN;
        if (out.state & STATE_CASTS) out.fixedState |= FIXED_CASTS;
        if (out.notTheEye)           out.fixedState |= FIXED_NOT_EYE;
        if (in.lods && in.lods->contains(entityIdx) && !in.lods->get(entityIdx).levels.empty()) {
            out.fixedState |= FIXED_LOD_SELECT;
        }
        if (hasWorld) out.fixedState |= FIXED_WORLD;
    }
    return true;
}

/**
 * @brief Whether a kept fixed mesh's entity still has the components it was worked out from.
 *
 * @param in    What it is worked out from.
 * @param index Its place in the Mesh storage.
 * @param fixed What m_fixed keeps for it.
 * @return False when it gained or lost a Transform, a WorldTransform or a picking LOD.
 */
bool keepsItsComponents(const ObjectInputs& in, uint32_t index, uint8_t fixed) {
    const uint32_t entityIdx = in.meshes.keyAt(index);
    const bool world = in.worldTransforms && in.worldTransforms->contains(entityIdx);
    const bool picks = in.lods && in.lods->contains(entityIdx) && !in.lods->get(entityIdx).levels.empty();
    return in.transforms.contains(entityIdx) && world == ((fixed & FIXED_WORLD) != 0)
        && picks == ((fixed & FIXED_LOD_SELECT) != 0);
}

} // namespace

void VisibilitySystem::publishCamera(
    const Camera& camera,
    const glm::vec3& position,
    const glm::quat& rotation,
    float viewportAspect,
    EntityId entity
) {
    Transform pose;
    pose.position = position;
    pose.rotation = rotation;

    const glm::mat4 projection = Camera::computeProjection(camera, viewportAspect);
    CameraData&     data       = m_result.camera;
    data = CameraData::from(Transform::computeView(pose), projection, position);
    data.viewmodelViewProjection = Camera::computeViewmodelProjection(camera, viewportAspect) * data.view;
    data.focusDistance = camera.focusDistance;
    data.dofAmount     = camera.dofAmount;
    data.dofMaxBlur    = camera.dofMaxBlur;

    m_result.cameraEntity = entity;
    m_result.hasCamera    = true;
}

bool VisibilitySystem::resolveCamera(const FrameContext& ctx, float viewportAspect) {
    const Scene& scene = ctx.scene;
    const HostView* host = ctx.hostView;
    if (host && !host->through) {
        publishCamera(host->camera, host->position, host->rotation, viewportAspect, {});
        return true;
    }

    // No fallback without an active camera: the frame draws nothing rather than
    // inheriting a host's last view.
    EntityId entity;
    if (host) {
        entity = host->through;
    } else {
        // A replaced world reuses slots, so the held camera could name a stranger.
        if (scene.epoch() != m_cameraEpoch) {
            m_cameraEpoch        = scene.epoch();
            m_cachedCameraEntity = {};
        }
        m_cachedCameraEntity = findActiveCamera(scene, m_cachedCameraEntity);
        entity = m_cachedCameraEntity;
    }
    const Camera*    camera    = scene.tryGet<Camera>(entity);
    const Transform* transform = scene.tryGet<Transform>(entity);
    if (!camera || !transform) return false;

    // A camera parented to a rig renders from its world pose, not its local offset.
    publishCamera(
        *camera,
        resolvedWorldPosition(scene, entity, *transform),
        resolvedWorldRotation(scene, entity, *transform),
        viewportAspect,
        entity
    );
    return true;
}

void VisibilitySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("VisibilitySystem");

    // Cleared here, not at the gather, so early returns publish an empty result, not last frame's.
    RenderObjects& objects = m_result.objects;
    objects.visible.clear();
    objects.scene.clear();
    objects.casterCount = 0;
    // An early return below leaves the columns as they were but untracked: whole, both now and
    // on the next frame that writes them.
    objects.changed.clear();
    objects.allChanged = true;
    m_result.hasCamera    = false;
    m_result.cameraEntity = {};

    // Cameras in auto-aspect mode (aspect <= 0) track the viewport.
    const HostChrome::ViewportRect viewport = ctx.chrome.viewport(ctx.window);
    const float vpW = static_cast<float>(viewport.width);
    const float vpH = static_cast<float>(viewport.height);
    const float viewportAspect = vpH > 0.0f ? vpW / vpH : 16.0f / 9.0f;

    if (!resolveCamera(ctx, viewportAspect)) {
        // A supported state (see RenderView::build); logged on the edge only, and not before the
        // world has run at all: a behavior's onStart, which may make the camera, has not either.
        const bool notRunYet = ctx.clock.getTick() == 0 && ctx.clock.isSimulationStill();
        if (!ctx.hostView && !m_noCameraLogged && !notRunYet) {
            LOG_WARNING("No active camera found for visibility");
            m_noCameraLogged = true;
        }
        m_sourcesValid = false;
        ctx.visibility = &m_result;
        return;
    }
    m_noCameraLogged = false;

    const CameraData& eye = m_result.camera;

    const float projScaleY = eye.projection[1][1];
    const float denom = projScaleY * vpH;
    const float screenThresholdSq = (denom > 0.0f)
        ? (ctx.render.cullMinPixels * ctx.render.cullMinPixels) / (denom * denom)
        : 0.0f;

    // A narrower view magnifies, so a level holds further out; orthographic keeps the reference.
    const bool  perspective      = eye.projection[3][3] == 0.0f;
    const bool  magnifies        = perspective && projScaleY > 0.0f;
    const float lodDistanceScale = magnifies ? LOD::REFERENCE_P11 / projScaleY : 1.0f;

    VisibilityContext context{
        .frustum               = Math::extractFrustum(eye.viewProjection),
        .cameraPosition        = eye.position,
        .view                  = eye.view,
        .minPixels             = ctx.render.cullMinPixels,
        .maxDistance           = ctx.render.cullMaxDistance,
        .maxDistanceSquared    = ctx.render.cullMaxDistance * ctx.render.cullMaxDistance,
        .screenSizeThresholdSq = screenThresholdSq,
        .lodDistanceScale      = lodDistanceScale,
        .perspective           = perspective,
    };

    // The sparse sets directly: the cull iterates them by index, in parallel.
    auto*       meshStorage           = ctx.scene.storage<Mesh>();
    auto*       transformStorage      = ctx.scene.storage<Transform>();
    const auto* worldTransformStorage = ctx.scene.storage<WorldTransform>();

    if (!meshStorage || !transformStorage) {
        m_sourcesValid = false;
        ctx.visibility = &m_result;
        return;
    }

    const uint32_t meshCount = static_cast<uint32_t>(meshStorage->size());

    const auto& resources = ctx.resources;
    const auto* lodStorage = ctx.scene.storage<LOD>();
    const PoseBuffer* poses = ctx.poses;

    // A `waves` surface the scene's ocean moves is carried past its mesh's box by as far as the
    // sea moves water, and casts no shadow: a held shadow tile would picture one moment of it.
    // With no ocean it draws, and casts, as any surface.
    const bool      sea   = ctx.ocean && ctx.ocean->present;
    const glm::vec2 swell = sea ? ctx.ocean->surface.farthestReach() : glm::vec2(0.0f);

    // Mesh::seenBy names a scene camera's view; a host's free view sees everything.
    const bool throughAnEye = static_cast<bool>(m_result.cameraEntity);
    // A weapon held nearer than the world's near plane is still in its own projection's view.
    const Math::Frustum viewmodelFrustum = Math::extractFrustum(eye.viewmodelViewProjection);

    // What every object's columns are made from beyond its components. A material is read only
    // for whether it rides a sea, so pulsing one changes nothing here while there is none.
    const ObjectSources sources{
        ctx.scene.epoch(),
        resources.epoch(),
        resources.revisionOf<MeshAsset>(),
        sea ? resources.revisionOf<MaterialAsset>() : 0,
        sea,
        throughAnEye
    };
    const ObjectMemberships memberships{
        transformStorage->structure(),
        worldTransformStorage ? worldTransformStorage->structure() : 0,
        lodStorage ? lodStorage->structure() : 0
    };
    // Last frame's columns compare index by index while the world and the asset graph hold; an
    // index whose stamp moved holds another Mesh, added or moved there by a removal.
    const bool comparable = m_sourcesValid && sources.sceneEpoch == m_sources.sceneEpoch
        && sources.assetEpoch == m_sources.assetEpoch;
    // A world the host may edit keeps nothing: the author moves fixed meshes too.
    const bool authoring = ctx.chrome.authoring();
    const bool keep      = comparable && sources == m_sources && !authoring;
    const bool recheck   = memberships != m_memberships;
    // The indices last frame's columns cover, as far as they compare.
    const uint32_t covered   = static_cast<uint32_t>(std::min<size_t>(m_stamps.size(), meshCount));
    const uint32_t lastCount = comparable ? covered : 0u;

    // A fixed mesh listed at an index past the end is gone, which no index below says.
    bool fixedGone = false;
    for (size_t i = meshCount; comparable && i < m_stamps.size(); ++i) {
        fixedGone = fixedGone || ((m_state[i] & STATE_LISTED) && objects.draws[i].fixed);
    }

    // Persistent: a steady scene allocates nothing. Each index's columns have one writer.
    m_state.resize(meshCount);
    m_fixed.resize(meshCount);
    m_stamps.resize(meshCount);
    objects.models.resize(meshCount);
    objects.bounds.resize(meshCount);
    objects.draws.resize(meshCount);
    objects.skinFirst.resize(meshCount);
    m_result.entities.resize(meshCount);

    const ObjectInputs inputs{
        ctx.scene,
        resources,
        *meshStorage,
        *transformStorage,
        worldTransformStorage,
        lodStorage,
        poses,
        context,
        sea,
        swell,
        throughAnEye
    };
    const uint32_t checkPhase = m_checkPhase;
    m_checkPhase = (m_checkPhase + 1) % FIXED_CHECK_STRIDE;
    std::atomic<bool> fixedMoved{false};
    std::atomic<bool> fixedChanged{fixedGone};

    {
        PROFILE_SCOPE("Visibility/Cull");
        parallelFor(meshCount, [&](size_t i) {
            const auto     idx   = static_cast<uint32_t>(i);
            const uint64_t stamp = meshStorage->stampAt(idx);
            // What this index held last frame, which may have been another Mesh.
            const bool    held   = i < lastCount && m_stamps[i] == stamp;
            const uint8_t listed = i < lastCount ? (m_state[i] & STATE_LISTED) : 0;

            uint8_t fixed = keep && held ? m_fixed[i] : 0;
            if ((fixed & FIXED_KEPT) && recheck && !keepsItsComponents(inputs, idx, fixed)) fixed = 0;

            if (fixed & FIXED_KEPT) {
                if (idx % FIXED_CHECK_STRIDE == checkPhase) {
                    ObjectColumns now;
                    const bool lodPicks = (fixed & FIXED_LOD_SELECT) != 0;
                    if (!computeObject(inputs, idx, now) || now.fixedState != fixed
                        || now.model != objects.models[i] || now.draw.material != objects.draws[i].material
                        || (!lodPicks && now.draw.mesh != objects.draws[i].mesh)
                        || now.bounds.min != objects.bounds[i].min
                        || now.bounds.max != objects.bounds[i].max) {
                        fixedMoved.store(true, std::memory_order_relaxed);
                    }
                }
                uint8_t state = 0;
                if (fixed & FIXED_DRAWN) state |= STATE_DRAWN;
                if (fixed & FIXED_CASTS) state |= STATE_CASTS;
                const Math::AABB& world = objects.bounds[i];
                if (fixed & FIXED_LOD_SELECT) {
                    const MeshHandle level = selectLOD(
                        meshStorage->dataAt(idx),
                        lodStorage,
                        meshStorage->keyAt(idx),
                        world.min,
                        world.max,
                        context
                    );
                    if (level != objects.draws[i].mesh) {
                        objects.draws[i].mesh = level;
                        state |= STATE_CHANGED;
                        fixedChanged.store(true, std::memory_order_relaxed);
                    }
                }
                if (!(fixed & FIXED_NOT_EYE) && Math::frustumIntersectsAABB(context.frustum, world)
                    && Culling::isNearEnough(world, context) && Culling::isLargeEnough(world, context)) {
                    state |= STATE_VISIBLE;
                }
                m_state[i] = state;
                return;
            }

            m_stamps[i] = stamp;
            ObjectColumns now;
            if (!computeObject(inputs, idx, now)) {
                // Hidden now: a change while the index was listed, though its columns stay as they were.
                m_state[i] = listed ? STATE_CHANGED : 0;
                m_fixed[i] = 0;
                if (listed && objects.draws[i].fixed) fixedChanged.store(true, std::memory_order_relaxed);
                return;
            }

            // Last frame's columns are only comparable while their sources hold; when they do not,
            // RenderObjects::allChanged says so and nothing is listed. Another Mesh's never are.
            const bool differs = !held || now.model != objects.models[i] || now.draw != objects.draws[i]
                || now.skinFirst != objects.skinFirst[i] || now.bounds.min != objects.bounds[i].min
                || now.bounds.max != objects.bounds[i].max;
            const bool changed = comparable && differs;
            const bool wasFixed = i < lastCount && objects.draws[i].fixed;

            objects.models[i]    = now.model;
            objects.bounds[i]    = now.bounds;
            objects.draws[i]     = now.draw;
            objects.skinFirst[i] = now.skinFirst;
            m_result.entities[i] = now.entity;
            m_fixed[i]           = authoring ? 0 : now.fixedState;

            const Math::Frustum& frustum = now.draw.viewmodel ? viewmodelFrustum : context.frustum;
            uint8_t state = now.state;
            // Shown or hidden, or a caster no longer, is a change too: the lists hold it or not.
            if (changed || (comparable && ((state & STATE_LISTED) != listed))) state |= STATE_CHANGED;
            if ((state & STATE_CHANGED) && (wasFixed || now.draw.fixed)) {
                fixedChanged.store(true, std::memory_order_relaxed);
            }
            if (!now.notTheEye && Math::frustumIntersectsAABB(frustum, now.bounds)
                && Culling::isNearEnough(now.bounds, context)
                && Culling::isLargeEnough(now.bounds, context)) {
                state |= STATE_VISIBLE;
            }
            m_state[i] = state;
        });
    }

    if (fixedMoved.load(std::memory_order_relaxed)) {
        // Kept until the next frame, which works every object out again; the warning, once a world.
        if (m_fixedMovedEpoch != sources.sceneEpoch) {
            LOG_WARNING("A Mesh with movable off moved or changed while the world ran; kept state rebuilt");
            m_fixedMovedEpoch = sources.sceneEpoch;
        }
        m_sourcesValid = false;
    } else {
        m_sources      = sources;
        m_memberships  = memberships;
        m_sourcesValid = true;
    }

    // Casters first, as the prefix RenderObjects::casterCount counts.
    PROFILE_SCOPE("Visibility/Gather");
    objects.allChanged = !comparable;
    ++objects.serial;
    if (objects.allChanged || fixedChanged.load(std::memory_order_relaxed)) ++objects.fixedSerial;
    for (uint32_t i = 0; i < meshCount; ++i) {
        const uint8_t state = m_state[i];
        if (state & STATE_VISIBLE) objects.visible.push_back(i);
        if ((state & (STATE_DRAWN | STATE_CASTS)) == (STATE_DRAWN | STATE_CASTS)) objects.scene.push_back(i);
        if (state & STATE_CHANGED) objects.changed.push_back(i);
    }
    objects.casterCount = static_cast<uint32_t>(objects.scene.size());
    for (uint32_t i = 0; i < meshCount; ++i) {
        if ((m_state[i] & (STATE_DRAWN | STATE_CASTS)) == STATE_DRAWN) objects.scene.push_back(i);
    }

    ctx.visibility = &m_result;
}

} // namespace Vkm::Engine
