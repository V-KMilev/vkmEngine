#include "system/render/render_view.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "logger.h"

#include "core/engine_config.h"
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
     * @brief Sort drawables by (materialType, material, mesh, castShadows) for optimal batching.
     *
     * Sort key (MSB -> LSB):
     *   bits 63-62 : MaterialType  (Opaque=0 first, then Transparent=1, then Unlit=2)
     *   bits 61-30 : material ID
     *   bits 29-1  : mesh ID       (capped at 29 bits)
     *   bit  0     : !castShadows  (shadow-casters sort to the front of each batch)
     */
    void sortDrawables(std::vector<DrawableData>& drawables) {
        if (drawables.size() <= 1) return;

        const uint32_t n = static_cast<uint32_t>(drawables.size());

        auto makeKey = [](const DrawableData& d) -> uint64_t {
            return (static_cast<uint64_t>(d.materialType)                  << 62)
                 | (static_cast<uint64_t>(d.material.id())                 << 30)
                 | (static_cast<uint64_t>(d.mesh.id() & 0x1FFFFFFFu)       <<  1)
                 | (d.castShadows ? 0ull : 1ull);
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
        drawable.castShadows  = mesh.castShadows;
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

        lightData.castShadows  = light.castShadows;
        lightData.shadowBias   = light.shadowBias;
        lightData.shadowExtent = light.shadowExtent;
        lightData.shadowSlot   = -1;  // assigned in the slot pass below

        // Use world-space position/rotation when HierarchySystem produced a WorldTransform
        if (scene.has<WorldTransform>(id)) {
            const glm::mat4& worldMatrix = scene.get<WorldTransform>(id).model;
            lightData.position = glm::vec3(worldMatrix[3]);
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

    // Assign shadow slots in light-iteration order, respecting per-type caps.
    // Slot == atlas layer (2D) or cube index (cube) and is later packed into
    // the GPU light data so the shader can index directly without a scan.
    uint32_t taken2D = 0;
    uint32_t takenCube = 0;
    bool csmAssigned = false;
    for (auto& light : lights) {
        if (!light.castShadows) continue;
        if (light.type == LightType::Point) {
            if (takenCube < Config::MaxShadowCastersCube) light.shadowSlot = static_cast<int>(takenCube++);
        } else if (light.type == LightType::Directional) {
            // The first directional caster owns the cascade block (NumCascades
            // consecutive 2D layers); shadowSlot is the base layer. Additional
            // directional casters get no shadow (kept simple - one sun).
            if (!csmAssigned && taken2D + Config::NumCascades <= Config::MaxShadowCasters2D) {
                light.shadowSlot = static_cast<int>(taken2D);
                taken2D += Config::NumCascades;
                csmAssigned = true;
            }
        } else { // Spot
            if (taken2D < Config::MaxShadowCasters2D) light.shadowSlot = static_cast<int>(taken2D++);
        }
    }
}

} // namespace Engine
