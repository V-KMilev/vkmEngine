#include "system/visibility/visibility_system.h"

#include <algorithm>
#include <cstring>

#include "logger.h"

#include "platform/threading/thread_pool.h"

#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "ecs/component/mesh.h"
#include "ecs/component/camera.h"
#include "ecs/component/transform.h"
#include "ecs/component/hierarchy.h"
#include "ecs/hierarchy_utils.h"

#include "system/visibility/bounds_utils.h"
#include "system/visibility/visibility_context.h"

#include "system/visibility/culling/frustum_culler.h"
#include "system/visibility/culling/screen_size_culling.h"
#include "system/visibility/culling/distance_culling.h"
#include "system/visibility/culling/occlusion_culler.h"

namespace Engine {

#include <chrono>
using Clock = std::chrono::high_resolution_clock;

#define TRACE_BEGIN(TAG) \
auto _start_##TAG = Clock::now();

#define TRACE_END(TAG) \
    auto _end_##TAG = Clock::now();

#define TRACE_GET_TIME(TAG) \
    std::chrono::duration_cast<std::chrono::microseconds>(_end_##TAG - _start_##TAG).count()


void VisibilitySystem::update(FrameContext& ctx) {
    static long long frameCount = 0;
    TRACE_BEGIN(single_threaded)
    updateSingleThreaded(ctx);
    TRACE_END(single_threaded)

    TRACE_BEGIN(multi_threaded)
    updateMultiThreaded(ctx);
    TRACE_END(multi_threaded)

    frameCount++;

    if (frameCount % 120 == 0) {
        LOG_INFO("Single-threaded time: %lld us", TRACE_GET_TIME(single_threaded));
        LOG_INFO("Multi-threaded time: %lld us", TRACE_GET_TIME(multi_threaded));
        LOG_INFO("Speedup: %.4fx", static_cast<double>(TRACE_GET_TIME(single_threaded)) / TRACE_GET_TIME(multi_threaded));
    }
}

void VisibilitySystem::updateSingleThreaded(FrameContext& ctx) {
    // Reuse persistent buffer - clear keeps capacity, avoiding per-frame allocation
    m_result.entries.clear();
    m_result.hasCamera = false;

    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPosition;
    float     cameraExposure = 1.0f;
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
    m_result.hasCamera      = true;

    const glm::mat4 viewProjection = projection * view;

    // Pre-compute screen-size threshold for sqrt-free test
    const float projScaleY = projection[1][1];
    const float vpHeight = static_cast<float>(ctx.viewportHeight);
    const float denom = projScaleY * vpHeight;
    const float screenThresholdSq = (denom > 0.0f)
        ? (m_minPixels * m_minPixels) / (denom * denom)
        : 0.0f;

    VisibilityContext context{
        .frustum        = extractFrustum(viewProjection),
        .cameraPosition = cameraPosition,
        .view           = view,
        .projection     = projection,

        .viewportWidth  = ctx.viewportWidth,
        .viewportHeight = ctx.viewportHeight,
        .minPixels      = m_minPixels,
        .maxDistance    = m_maxDistance,
        .maxDistanceSquared = m_maxDistance * m_maxDistance,
        .screenSizeThresholdSq = screenThresholdSq
    };

    // Get direct access to sparse sets for index-based parallel iteration
    auto* meshStorage      = ctx.scene.storage<Mesh>();
    auto* transformStorage = ctx.scene.storage<Transform>();
    const auto* hierarchyStorage = ctx.scene.storage<Hierarchy>();

    if (!meshStorage || !transformStorage) {
        ctx.visibility = &m_result;
        return;
    }

    const uint32_t meshCount = static_cast<uint32_t>(meshStorage->size());
    m_result.entries.reserve(meshCount);

    // Pre-compute world matrices for parented entities
    m_worldMatrixCache.clear();
    if (hierarchyStorage) {
        for (uint32_t i = 0; i < hierarchyStorage->size(); ++i) {
            const uint32_t entityIdx = hierarchyStorage->keyAt(i);
            const Hierarchy& hier = hierarchyStorage->dataAt(i);
            if (hier.parent && transformStorage->contains(entityIdx)) {
                const EntityId eid{entityIdx, ctx.scene.generationOf(entityIdx)};
                m_worldMatrixCache[entityIdx] = HierarchyUtils::computeWorldMatrix(ctx.scene, eid);
            }
        }
    }

    const auto& resources = ctx.resources;

    for (uint32_t i = 0; i < meshCount; ++i) {
        const uint32_t entityIdx = meshStorage->keyAt(i);
        const Mesh& mesh = meshStorage->dataAt(i);

        if (!mesh.visible) continue;
        if (!transformStorage->contains(entityIdx)) continue;

        const auto& meshAsset = resources.get(mesh.mesh);
        if (!hasValidBounds(meshAsset.boundsMin, meshAsset.boundsMax)) continue;

        const Transform& transform = transformStorage->get(entityIdx);

        const bool hasParent = hierarchyStorage
            && hierarchyStorage->contains(entityIdx)
            && hierarchyStorage->get(entityIdx).parent;

        const EntityId eid{entityIdx, ctx.scene.generationOf(entityIdx)};
        const glm::mat4 modelMatrix = hasParent
            ? m_worldMatrixCache.at(entityIdx)
            : Transform::computeModelMatrix(transform);

        glm::vec3 worldMin, worldMax;
        localToWorldAABB(
            modelMatrix,
            meshAsset.boundsMin,
            meshAsset.boundsMax,
            worldMin,
            worldMax
        );

        if (!FrustumCuller::isVisible(worldMin, worldMax, context)) continue;
        if (!DistanceCuller::isVisible(worldMin, worldMax, context)) continue;
        if (!ScreenSizeCuller::isVisible(worldMin, worldMax, context)) continue;

        m_result.entries.push_back({eid, modelMatrix});
    }

    ctx.visibility = &m_result;
}


void VisibilitySystem::updateMultiThreaded(FrameContext& ctx) {
    // Reuse persistent buffer - clear keeps capacity, avoiding per-frame allocation
    m_result.entries.clear();
    m_result.hasCamera = false;

    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPosition;
    float     cameraExposure = 1.0f;
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
    m_result.hasCamera      = true;

    const glm::mat4 viewProjection = projection * view;

    // Pre-compute screen-size threshold for sqrt-free test
    const float projScaleY = projection[1][1];
    const float vpHeight = static_cast<float>(ctx.viewportHeight);
    const float denom = projScaleY * vpHeight;
    const float screenThresholdSq = (denom > 0.0f)
        ? (m_minPixels * m_minPixels) / (denom * denom)
        : 0.0f;

    VisibilityContext context{
        .frustum        = extractFrustum(viewProjection),
        .cameraPosition = cameraPosition,
        .view           = view,
        .projection     = projection,

        .viewportWidth  = ctx.viewportWidth,
        .viewportHeight = ctx.viewportHeight,
        .minPixels      = m_minPixels,
        .maxDistance    = m_maxDistance,
        .maxDistanceSquared = m_maxDistance * m_maxDistance,
        .screenSizeThresholdSq = screenThresholdSq
    };

    // Get direct access to sparse sets for index-based parallel iteration
    auto* meshStorage      = ctx.scene.storage<Mesh>();
    auto* transformStorage = ctx.scene.storage<Transform>();
    const auto* hierarchyStorage = ctx.scene.storage<Hierarchy>();

    if (!meshStorage || !transformStorage) {
        ctx.visibility = &m_result;
        return;
    }

    const uint32_t meshCount = static_cast<uint32_t>(meshStorage->size());

    // Pre-compute world matrices for parented entities (serial - hierarchy traversal)
    m_worldMatrixCache.clear();
    if (hierarchyStorage) {
        for (uint32_t i = 0; i < hierarchyStorage->size(); ++i) {
            const uint32_t entityIdx = hierarchyStorage->keyAt(i);
            const Hierarchy& hier = hierarchyStorage->dataAt(i);
            if (hier.parent && transformStorage->contains(entityIdx)) {
                const EntityId eid{entityIdx, ctx.scene.generationOf(entityIdx)};
                m_worldMatrixCache[entityIdx] = HierarchyUtils::computeWorldMatrix(ctx.scene, eid);
            }
        }
    }

    const auto& resources = ctx.resources;

    // Persistent flat arrays - resize reuses capacity (no alloc after first frame).
    // Each thread writes to disjoint indices, so zero contention / zero atomics.
    m_visibleFlags.resize(meshCount);
    m_modelMatrices.resize(meshCount);

    std::memset(m_visibleFlags.data(), 0, meshCount);

    parallelFor(meshCount, [&](size_t i) {
        const auto idx = static_cast<uint32_t>(i);
        const uint32_t entityIdx = meshStorage->keyAt(idx);
        const Mesh& mesh = meshStorage->dataAt(idx);

        if (!mesh.visible) return;
        if (!transformStorage->contains(entityIdx)) return;

        const auto& meshAsset = resources.get(mesh.mesh);
        if (!hasValidBounds(meshAsset.boundsMin, meshAsset.boundsMax)) return;

        const Transform& transform = transformStorage->get(entityIdx);

        const bool hasParent = hierarchyStorage
            && hierarchyStorage->contains(entityIdx)
            && hierarchyStorage->get(entityIdx).parent;

        const glm::mat4 modelMatrix = hasParent
            ? m_worldMatrixCache.at(entityIdx)
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

        m_modelMatrices[i] = modelMatrix;
        m_visibleFlags[i] = 1;
    });

    // Serial gather - sequential reads, reuses persistent m_result.entries capacity
    m_result.entries.clear();
    for (uint32_t i = 0; i < meshCount; ++i) {
        if (!m_visibleFlags[i]) continue;
        const uint32_t entityIdx = meshStorage->keyAt(i);
        const EntityId eid{entityIdx, ctx.scene.generationOf(entityIdx)};
        m_result.entries.push_back({eid, m_modelMatrices[i]});
    }

    ctx.visibility = &m_result;
}

} // namespace Engine
