#define VKM_LOG_CATEGORY "VISIBILITY"

#include "system/visibility/visibility_system.h"

#include <algorithm>
#include <cstring>

#include "logger.h"

#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"
#include "platform/window/window_manager.h"

#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "ecs/component/mesh.h"
#include "ecs/component/lod.h"
#include "ecs/component/camera.h"
#include "ecs/component/transform.h"
#include "ecs/component/world_transform.h"

#include "core/math/bounds.h"
#include "system/visibility/visibility_context.h"

#include "system/visibility/culling/frustum_culler.h"
#include "system/visibility/culling/screen_size_culler.h"
#include "system/visibility/culling/distance_culler.h"

namespace Engine {

namespace {

/**
 * @brief Pick the geometry for this entity at this distance.
 *
 * Falls through to the Mesh component's own handle when the entity has no LOD
 * component, which is the common case and costs one storage lookup.
 *
 * Distance is measured to the bounds centre rather than the origin so a long
 * object does not pop when its pivot happens to sit far from its body.
 *
 * @return The chosen mesh; never empty when the Mesh component had one.
 */
template <typename LODStorage>
MeshHandle selectLOD(const Mesh& mesh, const LODStorage* lodStorage, uint32_t entityIdx,
                     const glm::vec3& worldMin, const glm::vec3& worldMax,
                     const VisibilityContext& context) {
    if (!lodStorage || !lodStorage->contains(entityIdx)) return mesh.mesh;

    const LOD& lod = lodStorage->get(entityIdx);
    if (lod.levels.empty()) return mesh.mesh;

    const glm::vec3 centre = (worldMin + worldMax) * 0.5f;
    const glm::vec3 delta  = centre - context.cameraPosition;
    const float distance   = glm::length(delta);
    const float scaled     = distance / glm::max(lod.bias, 0.001f);

    for (const LODLevel& level : lod.levels) {
        if (scaled <= level.maxDistance) return level.mesh ? level.mesh : mesh.mesh;
    }

    // Past the last threshold the coarsest level keeps drawing; removing the
    // entity is DistanceCuller's decision, not this one's.
    const MeshHandle& last = lod.levels.back().mesh;
    return last ? last : mesh.mesh;
}

} // namespace

bool VisibilitySystem::resolveActiveCamera(Scene& scene, float viewportAspect) {
    // No fallback when the scene has no active camera: the renderer publishes
    // hasCamera = false and draws nothing, rather than inheriting whatever the
    // editor's fly controls happen to still be pointed at.
    m_cachedCameraEntity = findActiveCamera(scene, m_cachedCameraEntity);
    if (!m_cachedCameraEntity) return false;

    const Camera&    camera    = scene.get<Camera>(m_cachedCameraEntity);
    const Transform& transform = scene.get<Transform>(m_cachedCameraEntity);

    // A camera parented to a rig (player root, boom arm) has to render from
    // its resolved world pose - the local Transform is only its offset
    // inside that rig.
    Transform pose = transform;
    pose.position  = resolvedWorldPosition(scene, m_cachedCameraEntity, transform);
    pose.rotation  = resolvedWorldRotation(scene, m_cachedCameraEntity, transform);

    m_result.projection     = Camera::computeProjection(camera, viewportAspect);
    m_result.view           = Transform::computeView(pose);
    m_result.cameraPosition = pose.position;
    m_result.focusDistance  = camera.focusDistance;
    m_result.dofAmount      = camera.dofAmount;
    m_result.hasCamera      = true;
    return true;
}

void VisibilitySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("VisibilitySystem");

    // Cleared here, not at the serial gather, so the early-return paths below
    // still publish an empty result instead of last frame's stale entries.
    m_result.entries.clear();
    m_result.shadowCasters.clear();
    m_result.hasCamera = false;

    // Cameras in auto-aspect mode (aspect <= 0) track the viewport.
    const float vpW = static_cast<float>(ctx.window.sceneViewportWidth());
    const float vpH = static_cast<float>(ctx.window.sceneViewportHeight());
    const float viewportAspect = vpH > 0.0f ? vpW / vpH : 16.0f / 9.0f;

    if (!resolveActiveCamera(ctx.scene, viewportAspect)) {
        // A supported state, not a failure: the render side has a documented
        // empty-snapshot path for it. Logged on the edge only - a scene between
        // cameras would otherwise repeat the line at frame rate.
        if (!m_noCameraLogged) {
            LOG_WARNING("No active camera found for visibility");
            m_noCameraLogged = true;
        }
        ctx.visibility = &m_result;
        return;
    }
    m_noCameraLogged = false;

    // resolveActiveCamera filled m_result.{view, projection, cameraPosition,
    // hasCamera}; downstream systems read those directly.
    const glm::mat4 viewProjection = m_result.projection * m_result.view;

    // Pre-compute screen-size threshold for sqrt-free test
    const float projScaleY = m_result.projection[1][1];
    const float vpHeight = static_cast<float>(ctx.window.sceneViewportHeight());
    const float denom = projScaleY * vpHeight;
    const float screenThresholdSq = (denom > 0.0f)
        ? (m_settings.minPixels * m_settings.minPixels) / (denom * denom)
        : 0.0f;

    VisibilityContext context{
        .frustum        = Math::extractFrustum(viewProjection),
        .cameraPosition = m_result.cameraPosition,
        .view           = m_result.view,
        .minPixels      = m_settings.minPixels,
        .maxDistance    = m_settings.maxDistance,
        .maxDistanceSquared = m_settings.maxDistance * m_settings.maxDistance,
        .screenSizeThresholdSq = screenThresholdSq,
    };

    // The sparse sets directly: the cull iterates them by index, in parallel.
    auto* meshStorage           = ctx.scene.storage<Mesh>();
    auto* transformStorage      = ctx.scene.storage<Transform>();
    const auto* worldTransformStorage = ctx.scene.storage<WorldTransform>();

    if (!meshStorage || !transformStorage) {
        ctx.visibility = &m_result;
        return;
    }

    const uint32_t meshCount = static_cast<uint32_t>(meshStorage->size());

    const auto& resources = ctx.resources;
    const auto* lodStorage = ctx.scene.storage<LOD>();

    // Persistent flat arrays - resize reuses capacity (no alloc after first frame).
    // Each thread writes to disjoint indices, so zero contention / zero atomics.
    m_visibleFlags.resize(meshCount);
    m_casterFlags.resize(meshCount);
    m_scratch.resize(meshCount);

    std::memset(m_visibleFlags.data(), 0, meshCount);
    std::memset(m_casterFlags.data(), 0, meshCount);

    {
        PROFILE_SCOPE("Visibility/Cull");
        parallelFor(meshCount, [&](size_t i) {
            const auto idx = static_cast<uint32_t>(i);
            const uint32_t entityIdx = meshStorage->keyAt(idx);
            const Mesh& mesh = meshStorage->dataAt(idx);

            if (!mesh.visible) return;
            if (!mesh.mesh) return;
            if (!transformStorage->contains(entityIdx)) return;

            const auto& meshAsset = resources.get(mesh.mesh);
            if (!Math::hasValidBounds(meshAsset.boundsMin, meshAsset.boundsMax)) return;

            const Transform& transform = transformStorage->get(entityIdx);

            const glm::mat4 modelMatrix = (worldTransformStorage && worldTransformStorage->contains(entityIdx))
                ? worldTransformStorage->get(entityIdx).model
                : Transform::computeModelMatrix(transform);

            glm::vec3 worldMin, worldMax;
            Math::localToWorldAABB(
                modelMatrix,
                meshAsset.boundsMin,
                meshAsset.boundsMax,
                worldMin,
                worldMax
            );

            // Filled for every valid mesh (not just camera-visible) so the caster
            // gather below can reach off-screen occluders. castShadows flags it.
            // The LOD level is resolved here because this is where the distance is
            // known. Shadow casters get the same level as the camera view: a
            // lower-detail silhouette is exactly as good for a depth map, and
            // picking separately would mean a second selection with no visible benefit.
            m_scratch[i] = VisibleEntity{
                ctx.scene.entityAt(entityIdx),
                modelMatrix,
                worldMin,
                worldMax,
                selectLOD(mesh, lodStorage, entityIdx, worldMin, worldMax, context)
            };
            m_casterFlags[i] = mesh.castShadows ? 1 : 0;

            // The camera-visibility culls only set the visible flag.
            if (!FrustumCuller::isVisible(worldMin, worldMax, context)) return;
            if (!DistanceCuller::isVisible(worldMin, worldMax, context)) return;
            if (!ScreenSizeCuller::isVisible(worldMin, worldMax, context)) return;

            m_visibleFlags[i] = 1;
        });
    }

    // Serial gather: sequential reads into the persistent m_result buffers,
    // already cleared at the top of update().
    PROFILE_SCOPE("Visibility/Gather");
    for (uint32_t i = 0; i < meshCount; ++i) {
        const bool visible = m_visibleFlags[i] != 0;
        const bool caster  = m_casterFlags[i]  != 0;
        if (!visible && !caster) continue;

        if (visible) m_result.entries.push_back(m_scratch[i]);
        if (caster)  m_result.shadowCasters.push_back(m_scratch[i]);
    }

    ctx.visibility = &m_result;
}

} // namespace Engine
