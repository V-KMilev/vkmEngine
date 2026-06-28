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
#include "ecs/component/camera.h"
#include "ecs/component/transform.h"
#include "ecs/component/world_transform.h"

#include "core/math/bounds.h"
#include "system/visibility/visibility_context.h"

#include "system/visibility/culling/frustum_culler.h"
#include "system/visibility/culling/screen_size_culling.h"
#include "system/visibility/culling/distance_culling.h"

namespace Engine {

bool VisibilitySystem::resolveActiveCamera(Scene& scene) {
    auto setCamera = [&](const Camera& camera, const Transform& transform) {
        m_result.projection     = Camera::computeProjection(camera);
        m_result.view           = Transform::computeView(transform);
        m_result.cameraPosition = transform.position;
        m_result.hasCamera      = true;
    };

    // Fast path: the cached camera entity (O(1) lookup).
    if (m_cachedCameraEntity
        && scene.isAlive(m_cachedCameraEntity)
        && scene.has<Camera>(m_cachedCameraEntity)
        && scene.has<Transform>(m_cachedCameraEntity))
    {
        const Camera& camera = scene.get<Camera>(m_cachedCameraEntity);
        if (camera.active) {
            setCamera(camera, scene.get<Transform>(m_cachedCameraEntity));
            return true;
        }
    }

    // Slow path: scan for the first active camera and re-cache it.
    m_cachedCameraEntity = {};
    bool found = false;
    scene.forEach<Camera, Transform>([&](EntityId id, const Camera& camera, const Transform& transform) {
        if (found || !camera.active) return;
        setCamera(camera, transform);
        m_cachedCameraEntity = id;
        found = true;
    });
    return found;
}

void VisibilitySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("VisibilitySystem");

    // Reuse persistent buffers - clear keeps capacity, avoiding per-frame allocation.
    // Cleared here, not at the serial gather, so the early-return paths below still
    // publish an empty result instead of last frame's stale entries/casters.
    m_result.entries.clear();
    m_result.shadowCasters.clear();
    m_result.hasCamera = false;

    if (!resolveActiveCamera(ctx.scene)) {
        LOG_ERROR("No active camera found for visibility");
        ctx.visibility = &m_result;
        return;
    }

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

    // Serial gather - sequential reads, reuses persistent m_result buffer capacity
    // (entries/shadowCasters were already cleared at the top of update()).
    PROFILE_SCOPE("Visibility/Gather");
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
