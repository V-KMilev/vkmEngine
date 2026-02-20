#include "visibility/visibility_system.h"

#include <algorithm>
#include <cstring>

#include "logger.h"

#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "ecs/component/mesh.h"
#include "ecs/component/camera.h"
#include "ecs/component/transform.h"
#include "ecs/component/hierarchy.h"
#include "ecs/hierarchy_utils.h"

#include "visibility/bounds_utils.h"
#include "visibility/visibility_context.h"

#include "visibility/culling/frustum_culler.h"
#include "visibility/culling/screen_size_culling.h"
#include "visibility/culling/distance_culling.h"
#include "visibility/culling/occlusion_culler.h"

#include "platform/threading/thread_pool.h"

namespace Engine {

void VisibilitySystem::update(FrameContext& ctx) {
    // Reuse persistent buffer - clear keeps capacity, avoiding per-frame allocation
    m_result.entities.clear();
    m_result.modelMatrices.clear();
    m_result.hasCamera = false;

    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPosition;
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
    m_result.entities.reserve(meshCount);
    m_result.modelMatrices.reserve(meshCount);

    auto& pool = ThreadPool::get();
    const size_t workerCount = pool.size();
    const size_t grain = std::max<size_t>(64, meshCount / (workerCount * 4));

    // Resize per-worker scratch buffers (reuses capacity across frames)
    if (m_workerEntities.size() != workerCount) {
        m_workerEntities.resize(workerCount);
        m_workerMatrices.resize(workerCount);
    }

    // Pre-reserve per-worker buffers based on estimated share (avoids reallocation during push_back)
    const size_t estimatePerWorker = (meshCount / workerCount) + 1;
    for (size_t i = 0; i < workerCount; ++i) {
        m_workerEntities[i].clear();
        m_workerMatrices[i].clear();
        m_workerEntities[i].reserve(estimatePerWorker);
        m_workerMatrices[i].reserve(estimatePerWorker);
    }

    const auto& resources = ctx.resources;

    pool.parallelFor(0, meshCount, grain,
        [&](size_t startIdx, size_t endIdx, size_t workerIdx) {
            auto& localEntities = m_workerEntities[workerIdx];
            auto& localMatrices = m_workerMatrices[workerIdx];

            for (size_t i = startIdx; i < endIdx; ++i) {
                const uint32_t denseIdx = static_cast<uint32_t>(i);
                const uint32_t entityIdx = meshStorage->keyAt(denseIdx);
                const Mesh& mesh = meshStorage->dataAt(denseIdx);

                if (!mesh.visible) continue;
                if (!transformStorage->contains(entityIdx)) continue;

                const auto& meshAsset = resources.get(mesh.mesh);
                if (!hasValidBounds(meshAsset.boundsMin, meshAsset.boundsMax)) continue;

                const Transform& transform = transformStorage->get(entityIdx);

                // Use hierarchy-aware world matrix if entity has a parent
                const bool hasParent = hierarchyStorage
                    && hierarchyStorage->contains(entityIdx)
                    && hierarchyStorage->get(entityIdx).parent;

                const EntityId eid{entityIdx, ctx.scene.generationOf(entityIdx)};
                const glm::mat4 modelMatrix = hasParent
                    ? HierarchyUtils::computeWorldMatrix(ctx.scene, eid)
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

                localEntities.push_back(eid);
                localMatrices.push_back(modelMatrix);
            }
        }
    );

    // Merge per-worker results: pre-size and memcpy via offset writes
    size_t totalVisible = 0;
    for (size_t i = 0; i < workerCount; ++i) {
        totalVisible += m_workerEntities[i].size();
    }
    m_result.entities.resize(totalVisible);
    m_result.modelMatrices.resize(totalVisible);

    size_t offset = 0;
    for (size_t i = 0; i < workerCount; ++i) {
        const size_t count = m_workerEntities[i].size();
        if (count > 0) {
            std::memcpy(m_result.entities.data() + offset,
                        m_workerEntities[i].data(), count * sizeof(EntityId));
            std::memcpy(m_result.modelMatrices.data() + offset,
                        m_workerMatrices[i].data(), count * sizeof(glm::mat4));
            offset += count;
        }
    }

    ctx.visibility = &m_result;
}

} // namespace Engine
