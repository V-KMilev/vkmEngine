#include "system/render/render_view.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "logger.h"

#include "ecs/scene.h"
#include "system/visibility/visibility.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/transform.h"
#include "ecs/component/world_transform.h"
#include "resource/resource_manager.h"

namespace Engine {

namespace {

    /**
     * @brief Sort drawables by (materialType, material, mesh) for optimal batching.
     *
     * Sort key: bits 63-62 = MaterialType, 61-32 = material ID, 31-0 = mesh ID.
     * Opaque (0) renders first, then Transparent (1), then Unlit (2).
     */
    void sortDrawables(std::vector<DrawableData>& drawables) {
        if (drawables.size() <= 1) return;

        const uint32_t n = static_cast<uint32_t>(drawables.size());

        auto makeKey = [](const DrawableData& d) -> uint64_t {
            return (static_cast<uint64_t>(d.materialType) << 62)
                 | (static_cast<uint64_t>(d.material.id()) << 32)
                 | d.mesh.id();
        };

        // Quick O(N) check: skip sort if already in order
        {
            bool alreadySorted = true;
            uint64_t prev = makeKey(drawables[0]);
            for (uint32_t i = 1; i < n; ++i) {
                uint64_t curr = makeKey(drawables[i]);
                if (curr < prev) { alreadySorted = false; break; }
                prev = curr;
            }
            if (alreadySorted) return;
        }

        // Phase 1: Sort lightweight keys (12-byte swaps vs ~88-byte DrawableData)
        static thread_local std::vector<std::pair<uint64_t, uint32_t>> sortKeys;
        sortKeys.resize(n);

        for (uint32_t i = 0; i < n; ++i) {
            sortKeys[i] = { makeKey(drawables[i]), i };
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
    camera.exposure       = visibility.cameraExposure;

    // Gather drawables - reserve only grows, never shrinks
    drawables.reserve(visibility.entries.size());

    // Guard against stale EntityIds (entity deleted between visibility and render).
    for (const auto& entry : visibility.entries) {
        if (!scene.isAlive(entry.id)) continue;
        const auto& mesh = scene.get<Mesh>(entry.id);

        DrawableData drawable;
        drawable.mesh         = mesh.mesh;
        drawable.material     = mesh.material;
        drawable.materialType = resources.get(mesh.material).type;
        drawable.model        = entry.model;
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

        // Use world-space position/rotation when HierarchySystem produced a WorldTransform
        if (scene.has<WorldTransform>(id)) {
            const glm::mat4& worldMatrix = scene.get<WorldTransform>(id).model;
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
