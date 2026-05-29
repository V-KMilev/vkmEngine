#define VKM_LOG_CATEGORY "VISIBILITY"

#include "system/visibility/visibility_system.h"

#include <algorithm>
#include <cstring>

#include "logger.h"

#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"

#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "ecs/component/mesh.h"
#include "ecs/component/camera.h"
#include "ecs/component/transform.h"
#include "ecs/component/world_transform.h"

#include "system/visibility/bounds_utils.h"
#include "system/visibility/visibility_context.h"

#include "system/visibility/culling/frustum_culler.h"
#include "system/visibility/culling/screen_size_culling.h"
#include "system/visibility/culling/distance_culling.h"
#include "system/visibility/culling/occlusion_culler.h"

namespace Engine {

void VisibilitySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("VisibilitySystem");

    // Reuse persistent buffer - clear keeps capacity, avoiding per-frame allocation
    m_result.entries.clear();
    m_result.hasCamera = false;

    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPosition;
    float     cameraExposure = 1.0f;
    float     cameraZNear    = 0.1f;
    float     cameraZFar     = 1000.0f;
    bool found = false;

    // Fast path: try the cached camera entity first (O(1) lookup)
    if (m_cachedCameraEntity
        && ctx.scene.isAlive(m_cachedCameraEntity)
        && ctx.scene.has<Camera>(m_cachedCameraEntity)
        && ctx.scene.has<Transform>(m_cachedCameraEntity))
    {
        const auto& camera = ctx.scene.get<Camera>(m_cachedCameraEntity);
        const auto& transform = ctx.scene.get<Transform>(m_cachedCameraEntity);
        if (camera.active) {
            projection     = Camera::computeProjection(camera);
            view           = Transform::computeView(transform);
            cameraPosition = transform.position;
            cameraExposure = camera.exposure;
            cameraZNear    = camera.zNear;
            cameraZFar     = camera.zFar;
            found = true;
        }
    }

    // Slow path: search all entities for an active camera
    if (!found) {
        m_cachedCameraEntity = {};
        ctx.scene.forEach<Camera, Transform>([&](EntityId id, const Camera& camera, const Transform& transform) {
            if (found) return;
            if (!camera.active) return;

            projection     = Camera::computeProjection(camera);
            view           = Transform::computeView(transform);
            cameraPosition = transform.position;
            cameraExposure = camera.exposure;
            cameraZNear    = camera.zNear;
            cameraZFar     = camera.zFar;
            m_cachedCameraEntity = id;
            found = true;
        });
    }

    if (!found) {
        LOG_ERROR("No active camera found for visibility");
        ctx.visibility = &m_result;
        return;
    }

    // Forward camera data so downstream systems avoid redundant lookups
    m_result.view           = view;
    m_result.projection     = projection;
    m_result.cameraPosition = cameraPosition;
    m_result.cameraExposure = cameraExposure;
    m_result.cameraZNear    = cameraZNear;
    m_result.cameraZFar     = cameraZFar;
    m_result.hasCamera      = true;

    const glm::mat4 viewProjection = projection * view;

    // Pre-compute screen-size threshold for sqrt-free test
    const float projScaleY = projection[1][1];
    const float vpHeight = static_cast<float>(ctx.viewportHeight);
    const float denom = projScaleY * vpHeight;
    const float screenThresholdSq = (denom > 0.0f)
        ? (m_settings.minPixels * m_settings.minPixels) / (denom * denom)
        : 0.0f;

    // Snapshot the Hi-Z occlusion pyramid ONCE per frame (it used to be
    // copied per-entity, under a mutex, inside OcclusionCuller::isVisible).
    // The local outlives the parallel cull below and every worker reads it
    // lock-free via context.occlusion. Cheap when occlusion is inactive: the
    // oracle hasn't published, so this is an empty, not-ready Frame.
    const OcclusionOracle::Frame occlusionFrame = OcclusionOracle::get().snapshot();

    VisibilityContext context{
        .frustum        = extractFrustum(viewProjection),
        .cameraPosition = cameraPosition,
        .view           = view,
        .projection     = projection,

        .viewportWidth  = ctx.viewportWidth,
        .viewportHeight = ctx.viewportHeight,
        .minPixels      = m_settings.minPixels,
        .maxDistance    = m_settings.maxDistance,
        .maxDistanceSquared = m_settings.maxDistance * m_settings.maxDistance,
        .screenSizeThresholdSq = screenThresholdSq,
        .occlusion      = &occlusionFrame
    };

    // Get direct access to sparse sets for index-based parallel iteration
    auto* meshStorage           = ctx.scene.storage<Mesh>();
    auto* transformStorage      = ctx.scene.storage<Transform>();
    const auto* worldTransformStorage = ctx.scene.storage<WorldTransform>();

    if (!meshStorage || !transformStorage) {
        ctx.visibility = &m_result;
        return;
    }

    const uint32_t meshCount = static_cast<uint32_t>(meshStorage->size());

    const auto& resources = ctx.resources;

    // Persistent flat arrays - resize reuses capacity (no alloc after first frame).
    // Each thread writes to disjoint indices, so zero contention / zero atomics.
    m_visibleFlags.resize(meshCount);
    m_modelMatrices.resize(meshCount);
    m_worldMins.resize(meshCount);
    m_worldMaxs.resize(meshCount);

    std::memset(m_visibleFlags.data(), 0, meshCount);

    {
        PROFILE_SCOPE("Visibility/Cull");
        parallelFor(meshCount, [&](size_t i) {
        const auto idx = static_cast<uint32_t>(i);
        const uint32_t entityIdx = meshStorage->keyAt(idx);
        const Mesh& mesh = meshStorage->dataAt(idx);

        if (!mesh.visible) return;
        if (!mesh.mesh) return;  // unresolved Mesh component (empty asset slot)
        if (!transformStorage->contains(entityIdx)) return;

        const auto& meshAsset = resources.get(mesh.mesh);
        if (!hasValidBounds(meshAsset.boundsMin, meshAsset.boundsMax)) return;

        const Transform& transform = transformStorage->get(entityIdx);

        const glm::mat4 modelMatrix = (worldTransformStorage && worldTransformStorage->contains(entityIdx))
            ? worldTransformStorage->get(entityIdx).model
            : Transform::computeModelMatrix(transform);

        glm::vec3 worldMin, worldMax;
        localToWorldAABB(
            modelMatrix,
            meshAsset.boundsMin,
            meshAsset.boundsMax,
            worldMin,
            worldMax
        );

        if (!FrustumCuller::isVisible(worldMin, worldMax, context)) return;
        if (!DistanceCuller::isVisible(worldMin, worldMax, context)) return;
        if (!ScreenSizeCuller::isVisible(worldMin, worldMax, context)) return;
        // Hi-Z occlusion: 1-frame stale, conservative (visible on miss).
        // Returns visible unless OcclusionOracle has a published pyramid
        // and the AABB is provably behind every cell its footprint covers.
        if (!OcclusionCuller::isVisible(worldMin, worldMax, context)) return;

        m_modelMatrices[i] = modelMatrix;
        m_worldMins[i]     = worldMin;
        m_worldMaxs[i]     = worldMax;
        m_visibleFlags[i]  = 1;
        });
    }

    // Serial gather - sequential reads, reuses persistent m_result.entries capacity
    PROFILE_SCOPE("Visibility/Gather");
    m_result.entries.clear();
    for (uint32_t i = 0; i < meshCount; ++i) {
        if (!m_visibleFlags[i]) continue;
        const uint32_t entityIdx = meshStorage->keyAt(i);
        const EntityId eid{entityIdx, ctx.scene.generationOf(entityIdx)};
        m_result.entries.push_back({eid, m_modelMatrices[i], m_worldMins[i], m_worldMaxs[i]});
    }

    ctx.visibility = &m_result;
}

} // namespace Engine
