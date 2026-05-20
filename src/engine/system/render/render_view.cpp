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
     *   bits 63-62 : sort priority (Opaque, AlphaMask, Unlit, Transparent)
     *   bits 61-30 : material ID
     *   bits 29-1  : mesh ID       (capped at 29 bits)
     *   bit  0     : !castShadows  (shadow-casters sort to the front of each batch)
     */
    void sortDrawables(std::vector<DrawableData>& drawables) {
        if (drawables.size() <= 1) return;

        const uint32_t n = static_cast<uint32_t>(drawables.size());

        // Render-order priority is decoupled from MaterialType's raw enum
        // value so we can insert new types (e.g. AlphaMask between Opaque
        // and Transparent) without renumbering the enum. Order matters: the
        // forward pass needs all depth-writing material classes drawn
        // before Transparent so the per-batch HDR snapshot includes them.
        auto sortPriority = [](MaterialType t) -> uint64_t {
            switch (t) {
                case MaterialType::Opaque:      return 0;
                case MaterialType::AlphaMask:   return 1;
                case MaterialType::Unlit:       return 2;
                case MaterialType::Transparent: return 3;
            }
            return 0;
        };

        auto makeKey = [&](const DrawableData& d) -> uint64_t {
            return (sortPriority(d.materialType)                           << 62)
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

    /**
     * @brief Sort the contiguous Transparent run back-to-front from the camera.
     *
     * sortDrawables() groups by (materialType, material, mesh) for batching,
     * which leaves transparents in an arbitrary order along Z. The transparent
     * forward phase draws into HDR with depth-write off and refreshes its
     * opaque-scene snapshot after every batch, so back-to-front ordering means
     * (a) blending composites correctly and (b) closer panes refract the
     * panes behind them. Centroid = the model's translation column; cheap and
     * good enough for non-degenerate transparents (intersecting / huge
     * transparents are an inherent limit of per-object sorting).
     */
    void sortTransparentsByDepth(std::vector<DrawableData>& drawables,
                                 const glm::vec3& camPos) {
        const size_t n = drawables.size();

        size_t lo = 0;
        while (lo < n && drawables[lo].materialType != MaterialType::Transparent) ++lo;
        size_t hi = lo;
        while (hi < n && drawables[hi].materialType == MaterialType::Transparent) ++hi;
        if (hi - lo <= 1) return;

        auto distSq = [&](const DrawableData& d) {
            const glm::vec3 v = glm::vec3(d.model[3]) - camPos;
            return v.x * v.x + v.y * v.y + v.z * v.z;
        };

        std::sort(drawables.begin() + lo, drawables.begin() + hi,
            [&](const DrawableData& a, const DrawableData& b) {
                return distSq(a) > distSq(b);  // farthest first
            });
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
    shadowCasters.clear();
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

    // Sort drawables for optimal batching, then sub-sort just the
    // transparent run back-to-front (so blending order is correct and the
    // per-batch refraction snapshot in the transparent forward phase shows
    // panes-behind-panes correctly refracted through).
    sortDrawables(drawables);
    sortTransparentsByDepth(drawables, camera.position);

    // Shadow casters: every shadow-casting mesh in the scene, independent of
    // the camera frustum. The shadow pass needs occluders that are off-screen
    // (behind/beside the camera, or only their shadow is in view); culling
    // these to the camera frustum is what made Sponza's shadows flicker and
    // vanish on view changes. Use the hierarchy's world matrix when present.
    shadowCasters.reserve(drawables.size());
    scene.forEach<Mesh>([&](EntityId id, const Mesh& mesh) {
        if (!mesh.visible || !mesh.castShadows) return;
        if (!scene.has<Transform>(id)) return;

        DrawableData caster;
        caster.mesh         = mesh.mesh;
        caster.material     = mesh.material;
        caster.materialType = resources.get(mesh.material).type;
        caster.castShadows  = true;
        caster.model        = scene.has<WorldTransform>(id)
            ? scene.get<WorldTransform>(id).model
            : Transform::computeModelMatrix(scene.get<Transform>(id));
        shadowCasters.emplace_back(caster);
    });
    sortDrawables(shadowCasters);

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
