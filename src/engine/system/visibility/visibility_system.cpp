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

namespace Engine {

void VisibilitySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("VisibilitySystem");

    // Reuse persistent buffer - clear keeps capacity, avoiding per-frame allocation
    m_result.entries.clear();
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
        ? (m_settings.minPixels * m_settings.minPixels) / (denom * denom)
        : 0.0f;

    VisibilityContext context{
        .frustum        = Math::extractFrustum(viewProjection),
        .cameraPosition = cameraPosition,
        .view           = view,
        .minPixels      = m_settings.minPixels,
        .maxDistance    = m_settings.maxDistance,
        .maxDistanceSquared = m_settings.maxDistance * m_settings.maxDistance,
        .screenSizeThresholdSq = screenThresholdSq,
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
    m_casterFlags.resize(meshCount);
    m_modelMatrices.resize(meshCount);
    m_worldMins.resize(meshCount);
    m_worldMaxs.resize(meshCount);

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

            // Stored for every valid mesh (not just camera-visible) so the caster
            // gather below can reach off-screen occluders. castShadows flags it.
            m_modelMatrices[i] = modelMatrix;
            m_worldMins[i]     = worldMin;
            m_worldMaxs[i]     = worldMax;
            m_casterFlags[i]   = mesh.castShadows ? 1 : 0;

            // The camera-visibility culls only set the visible flag.
            if (!FrustumCuller::isVisible(worldMin, worldMax, context)) return;
            if (!DistanceCuller::isVisible(worldMin, worldMax, context)) return;
            if (!ScreenSizeCuller::isVisible(worldMin, worldMax, context)) return;

            m_visibleFlags[i]  = 1;
        });
    }

    // Serial gather - sequential reads, reuses persistent m_result.entries capacity
    PROFILE_SCOPE("Visibility/Gather");
    m_result.entries.clear();
    m_result.shadowCasters.clear();
    for (uint32_t i = 0; i < meshCount; ++i) {
        const bool visible = m_visibleFlags[i] != 0;
        const bool caster  = m_casterFlags[i]  != 0;
        if (!visible && !caster) continue;

        const uint32_t entityIdx = meshStorage->keyAt(i);
        const EntityId eid{entityIdx, ctx.scene.generationOf(entityIdx)};
        const VisibleEntity entry{eid, m_modelMatrices[i], m_worldMins[i], m_worldMaxs[i]};

        if (visible) m_result.entries.push_back(entry);
        if (caster)  m_result.shadowCasters.push_back(entry);
    }

    ctx.visibility = &m_result;
}

} // namespace Engine
