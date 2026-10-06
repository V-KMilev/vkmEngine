#define VKM_LOG_CATEGORY "VISIBILITY"

#include "system/visibility/visibility_system.h"

#include <cstring>

#include <glm/gtc/constants.hpp>

#include "logger.h"

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
    m_result.hasCamera    = false;
    m_result.cameraEntity = {};

    // Cameras in auto-aspect mode (aspect <= 0) track the viewport.
    const HostChrome::ViewportRect viewport = ctx.chrome.viewport(ctx.window);
    const float vpW = static_cast<float>(viewport.width);
    const float vpH = static_cast<float>(viewport.height);
    const float viewportAspect = vpH > 0.0f ? vpW / vpH : 16.0f / 9.0f;

    if (!resolveCamera(ctx, viewportAspect)) {
        // A supported state (see RenderView::build); logged on the edge only.
        if (!ctx.hostView && !m_noCameraLogged) {
            LOG_WARNING("No active camera found for visibility");
            m_noCameraLogged = true;
        }
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
        ctx.visibility = &m_result;
        return;
    }

    const uint32_t meshCount = static_cast<uint32_t>(meshStorage->size());

    const auto& resources = ctx.resources;
    const auto* lodStorage = ctx.scene.storage<LOD>();
    const PoseBuffer* poses = ctx.poses;

    // Persistent: a steady scene allocates nothing. Each index has one writer, so no atomics.
    m_state.resize(meshCount);
    objects.models.resize(meshCount);
    objects.bounds.resize(meshCount);
    objects.draws.resize(meshCount);
    objects.skinFirst.resize(meshCount);
    m_result.entities.resize(meshCount);

    std::memset(m_state.data(), 0, meshCount);

    {
        PROFILE_SCOPE("Visibility/Cull");
        parallelFor(meshCount, [&](size_t i) {
            const auto idx = static_cast<uint32_t>(i);
            const uint32_t entityIdx = meshStorage->keyAt(idx);
            const Mesh& mesh = meshStorage->dataAt(idx);

            if (!mesh.visible) return;
            // Nothing to shade it with draws and casts nothing, decided once for every list.
            if (!mesh.mesh || !mesh.material) return;
            if (!transformStorage->contains(entityIdx)) return;

            const auto& meshAsset = resources.get(mesh.mesh);
            if (!meshAsset.bounds().valid()) return;

            const Transform& transform = transformStorage->get(entityIdx);

            const bool hasWorld = worldTransformStorage && worldTransformStorage->contains(entityIdx);
            const glm::mat4 modelMatrix = hasWorld
                ? worldTransformStorage->get(entityIdx).model
                : Transform::computeModelMatrix(transform);

            // The pose sizes a skinned mesh's box; an unskinned one keeps its bind bounds.
            const bool skinned = poses && !meshAsset.skin.empty();
            const PoseSlice* slice = skinned ? poses->sliceOf(ctx.scene.entityAt(entityIdx)) : nullptr;
            glm::vec3 localMin, localMax;
            poseLocalBounds(meshAsset, slice, localMin, localMax);

            const Math::AABB world = Math::transform(modelMatrix, {localMin, localMax});

            // Every valid mesh, not just visible ones, so the scene-wide gather reaches
            // off-screen occluders.
            objects.models[i]      = modelMatrix;
            objects.bounds[i]      = world;
            objects.draws[i]       = ObjectDraw{
                selectLOD(mesh, lodStorage, entityIdx, world.min, world.max, context),
                mesh.material,
                slice ? slice->count : 0u
            };
            objects.skinFirst[i]   = slice ? slice->first : 0u;
            m_result.entities[i]   = ctx.scene.entityAt(entityIdx);

            uint8_t state = STATE_DRAWN;
            if (mesh.castShadows) state |= STATE_CASTS;

            if (Math::frustumIntersectsAABB(context.frustum, world)
                && Culling::isNearEnough(world, context)
                && Culling::isLargeEnough(world, context)) {
                state |= STATE_VISIBLE;
            }
            m_state[i] = state;
        });
    }

    // Casters first, as the prefix RenderObjects::casterCount counts.
    PROFILE_SCOPE("Visibility/Gather");
    for (uint32_t i = 0; i < meshCount; ++i) {
        const uint8_t state = m_state[i];
        if ((state & STATE_DRAWN) == 0) continue;

        if (state & STATE_VISIBLE) objects.visible.push_back(i);
        if (state & STATE_CASTS)   objects.scene.push_back(i);
    }
    objects.casterCount = static_cast<uint32_t>(objects.scene.size());
    for (uint32_t i = 0; i < meshCount; ++i) {
        if ((m_state[i] & (STATE_DRAWN | STATE_CASTS)) == STATE_DRAWN) objects.scene.push_back(i);
    }

    ctx.visibility = &m_result;
}

} // namespace Vkm::Engine
