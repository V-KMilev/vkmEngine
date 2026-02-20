#include "render/render_view.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "logger.h"

#include "ecs/scene.h"
#include "visibility/visibility.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/transform.h"
#include "ecs/component/hierarchy.h"
#include "ecs/hierarchy_utils.h"
#include "resource/resource_manager.h"

namespace Engine {

namespace {

    /**
     * @brief Sort drawables by (material, mesh) for optimal batching.
     *
     * Indirect sort: sorts lightweight (uint64 key, uint32 index) pairs
     * with a custom comparator on key only (avoids std::pair's lexicographic
     * operator<). Swaps 12 bytes instead of ~80 per DrawableData, then
     * gathers into sorted order in one pass.
     */
    void sortDrawables(std::vector<DrawableData>& drawables) {
        if (drawables.size() <= 1) return;

        const uint32_t n = static_cast<uint32_t>(drawables.size());

        // Quick O(N) check: skip sort if already in order (common when
        // visibility order is stable frame-to-frame)
        {
            bool alreadySorted = true;
            uint64_t prev = (static_cast<uint64_t>(drawables[0].material.id()) << 32) | drawables[0].mesh.id();
            for (uint32_t i = 1; i < n; ++i) {
                uint64_t curr = (static_cast<uint64_t>(drawables[i].material.id()) << 32) | drawables[i].mesh.id();
                if (curr < prev) { alreadySorted = false; break; }
                prev = curr;
            }
            if (alreadySorted) return;
        }

        // Phase 1: Sort lightweight keys (12-byte swaps vs 80-byte DrawableData)
        static thread_local std::vector<std::pair<uint64_t, uint32_t>> sortKeys;
        sortKeys.resize(n);

        for (uint32_t i = 0; i < n; ++i) {
            sortKeys[i] = {
                (static_cast<uint64_t>(drawables[i].material.id()) << 32) | drawables[i].mesh.id(),
                i
            };
        }

        // Custom comparator: compare key only (NOT default pair lexicographic)
        std::sort(sortKeys.begin(), sortKeys.end(),
            [](const std::pair<uint64_t, uint32_t>& a, const std::pair<uint64_t, uint32_t>& b) {
                return a.first < b.first;
            });

        // Phase 2: Gather into sorted order (sequential write, random read)
        static thread_local std::vector<DrawableData> sorted;
        sorted.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            sorted[i] = drawables[sortKeys[i].second];
        }

        drawables.swap(sorted);
    }
}

void RenderView::build(
    const Scene& scene,
    const ResourceManager& resources,
    const Visibility& visibility,
    uint32_t viewportWidth,
    uint32_t viewportHeight
) {
    // Clear vectors but keep capacity from previous frame
    drawables.clear();
    lights.clear();

    this->viewportWidth  = viewportWidth;
    this->viewportHeight = viewportHeight;

    // Use camera data already computed by VisibilitySystem (avoids redundant scene search)
    if (!visibility.hasCamera) {
        LOG_ERROR("No active camera found");
        return;
    }

    camera.view           = visibility.view;
    camera.projection     = visibility.projection;
    camera.viewProjection = visibility.projection * visibility.view;
    camera.position       = visibility.cameraPosition;

    // Gather drawables - reserve only grows, never shrinks
    drawables.reserve(visibility.entities.size());

    // Iterate through entities and matrices in lockstep (same order, cache-friendly).
    // Guard against stale EntityIds (entity deleted between visibility and render).
    for (size_t i = 0; i < visibility.entities.size(); ++i) {
        if (!scene.isAlive(visibility.entities[i])) continue;
        const auto& mesh = scene.get<Mesh>(visibility.entities[i]);

        DrawableData drawable;
        drawable.mesh     = mesh.mesh;
        drawable.material = mesh.material;
        drawable.model    = visibility.modelMatrices[i];
        drawables.emplace_back(drawable);
    }

    // Sort drawables for optimal batching
    sortDrawables(drawables);

    // Gather lights (dense iteration, no holes)
    lights.reserve(scene.count<Light>());

    scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& transform) {
        if (!light.enabled) return;

        LightData lightData;
        lightData.type = light.type;
        lightData.color = light.color;
        lightData.intensity = light.intensity;
        lightData.radius = light.radius;
        lightData.innerConeAngle = light.innerConeAngle;
        lightData.outerConeAngle = light.outerConeAngle;
        lightData.castShadows = light.castShadows;

        // Use world-space position/rotation for lights with hierarchy parents
        if (scene.has<Hierarchy>(id) && scene.get<Hierarchy>(id).parent) {
            const glm::mat4 worldMatrix = HierarchyUtils::computeWorldMatrix(scene, id);
            lightData.position = glm::vec3(worldMatrix[3]);
            // Extract world rotation: normalize columns to remove scale, then quat_cast
            lightData.rotation = glm::quat_cast(glm::mat3(
                glm::normalize(glm::vec3(worldMatrix[0])),
                glm::normalize(glm::vec3(worldMatrix[1])),
                glm::normalize(glm::vec3(worldMatrix[2]))
            ));
        } else {
            lightData.position = transform.position;
            lightData.rotation = transform.rotation;
        }

        lights.emplace_back(lightData);
    });
}

} // namespace Engine
