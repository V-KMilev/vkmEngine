#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/render_view.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "logger.h"

#include "core/engine_config.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "system/visibility/visibility.h"
#include "ecs/component/mesh.h"
#include "ecs/component/mesh_lod.h"
#include "ecs/component/light.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/selected.h"
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
void sortTransparentsByDepth(
    std::vector<DrawableData>& drawables,
    const glm::vec3& camPos
) {
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

// Pick a renderable's LOD level from its projected screen size: the coarsest
// level whose switch-height threshold the bounding sphere's pixel height falls
// under. Returns that level's mesh handle.
inline MeshHandle selectLOD(
    const MeshLOD& lod,
    const glm::vec3& worldMin,
    const glm::vec3& worldMax,
    const CameraData& camera,
    uint32_t viewportHeight
) {
    if (lod.count <= 1) return lod.levels[0];

    const glm::vec3 center = (worldMin + worldMax) * 0.5f;
    const float radius = 0.5f * glm::length(worldMax - worldMin);
    const float dist   = glm::length(center - camera.position);
    // Projected pixel height of the bounding sphere. NDC height
    // 2*radius*projScaleY/dist spans the [-1,1] range = viewportHeight pixels,
    // so the 2 and the /2 cancel.
    const float projScaleY = camera.projection[1][1];
    const float pixelHeight = (dist > 1.0e-4f)
        ? (radius * projScaleY / dist) * static_cast<float>(viewportHeight)
        : 1.0e9f;

    int level = 0;
    for (int i = 1; i < lod.count; ++i) {
        if (pixelHeight < lod.switchHeights[i]) level = i;
    }
    return lod.levels[level];
}

// Fold a 64-bit value into a running hash (boost::hash_combine mix).
inline uint64_t hashCombine(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

/**
 * @brief Build the sorted shadow-caster list into @p out, reusing the cached
 *        structure when nothing relevant changed.
 *
 * Pass 1 walks every (Mesh, Transform) entity and folds each caster's identity
 * + sort-key fields into a fingerprint. Because the caster filter reads the
 * material type live, a material flipping Opaque<->non-Opaque changes the
 * membership and therefore the fingerprint - no separate material-version
 * check is needed; the global version guards a whole-ResourceManager swap
 * (scene load) where ids could otherwise coincidentally collide.
 *
 * On a fingerprint match the cached sorted identities are reused as-is; only
 * the matrices are refreshed (pass 3), so movement never triggers a rebuild.
 * On a mismatch the identities are re-collected and re-sorted (pass 2).
 */
template <typename MaterialTypeFn>
void buildShadowCasters(
    std::vector<DrawableData>& out,
    ShadowCasterCache&         cache,
    const Scene&               scene,
    const ResourceManager&     resources,
    MaterialTypeFn&&           materialTypeOf
) {
    auto isCaster = [&](const Mesh& mesh) -> bool {
        return mesh.visible && mesh.castShadows && mesh.mesh && mesh.material
            && materialTypeOf(mesh.material) == MaterialType::Opaque;
    };

    // Pass 1: structural fingerprint over the caster set.
    const uint64_t globalVersion = resources.getGlobalVersion();
    uint64_t fp    = 0;
    uint32_t count = 0;
    scene.forEach<Mesh, Transform>([&](EntityId id, const Mesh& mesh, const Transform&) {
        if (!isCaster(mesh)) return;
        fp = hashCombine(fp, (static_cast<uint64_t>(id.index) << 32) ^ id.generation);
        // Fold the FULL resource handles (index AND generation), not just .id()
        // (= slot index). Resource slots never recycle today (nothing frees a
        // mesh/material), but if a single-asset delete/recreate path is ever
        // added, a recycled slot index with a bumped generation must change the
        // fingerprint - otherwise the cache would serve a stale handle. Free:
        // generations are constant while no slot is freed.
        fp = hashCombine(fp, (static_cast<uint64_t>(mesh.mesh.key.index) << 32) ^ mesh.mesh.key.generation);
        fp = hashCombine(fp, (static_cast<uint64_t>(mesh.material.key.index) << 32) ^ mesh.material.key.generation);
        ++count;
    });

    const bool unchanged = cache.valid
        && globalVersion == cache.globalVersion
        && count == cache.count
        && fp == cache.fingerprint;

    // Pass 2 (only on a structural change): re-collect + re-sort the caster
    // identities. Sort by (material.id, mesh.id) so identical (material, mesh)
    // casters are contiguous for the shadow instance batcher - matches the
    // effective sortDrawables key for an all-Opaque, all-castShadows set.
    if (!unchanged) {
        cache.sorted.clear();
        cache.sorted.reserve(count);
        scene.forEach<Mesh, Transform>([&](EntityId id, const Mesh& mesh, const Transform&) {
            if (!isCaster(mesh)) return;
            cache.sorted.push_back({id, mesh.mesh, mesh.material});
        });
        std::sort(cache.sorted.begin(), cache.sorted.end(),
            [](const ShadowCasterCache::Entry& a, const ShadowCasterCache::Entry& b) {
                if (a.material.id() != b.material.id()) return a.material.id() < b.material.id();
                return a.mesh.id() < b.mesh.id();
            });
        cache.fingerprint   = fp;
        cache.count         = count;
        cache.globalVersion = globalVersion;
        cache.valid         = true;
    }

    // Pass 3: refresh matrices live from the current world transform (or the
    // local transform fallback) - mirrors the old gather's matrix source.
    // Storages hoisted once instead of a has()+get() probe pair per caster.
    const auto* worldStorage     = scene.storage<WorldTransform>();
    const auto* transformStorage = scene.storage<Transform>();
    out.resize(cache.sorted.size());
    for (std::size_t i = 0; i < cache.sorted.size(); ++i) {
        const ShadowCasterCache::Entry& e = cache.sorted[i];
        DrawableData& d = out[i];
        d.mesh         = e.mesh;
        d.material     = e.material;
        d.materialType = MaterialType::Opaque;
        d.castShadows  = true;
        if (worldStorage && worldStorage->contains(e.entity.index)) {
            d.model = worldStorage->get(e.entity.index).model;
        } else if (transformStorage && transformStorage->contains(e.entity.index)) {
            d.model = Transform::computeModelMatrix(transformStorage->get(e.entity.index));
        } else {
            d.model = glm::mat4(1.0f);
        }
    }
}
}

void RenderView::build(
    const Scene& scene,
    const ResourceManager& resources,
    const Visibility& visibility,
    uint32_t viewportWidth,
    uint32_t viewportHeight,
    ShadowCasterCache& shadowCache
) {
    PROFILE_SCOPE("RenderView::build");

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
    camera.zNear          = visibility.cameraZNear;
    camera.zFar           = visibility.cameraZFar;

    // Gather drawables - reserve only grows, never shrinks
    drawables.reserve(visibility.entries.size());

    // Persistent MaterialType cache, indexed by material id (0 = unknown,
    // else type + 1). materialTypeOf is called once per drawable AND once per
    // mesh in the shadow-caster fingerprint walk, so at 13k entities the old
    // per-frame-cleared linear-scan memo cost up to O(visible + meshes) x
    // O(unique materials) comparisons every frame. This cache is rebuilt only
    // when materials are edited (MaterialAsset type-version bumps on commit -
    // covers any .type change) or the ResourceManager is swapped (global
    // version bumps on scene load); otherwise lookup is O(1). A material added
    // at a fresh id reads 0 (unknown) and is fetched on first use even without
    // a version bump.
    static thread_local std::vector<uint8_t> matTypeCache;
    static thread_local uint64_t matTypeCacheGlobal = ~0ull;
    static thread_local uint64_t matTypeCacheType   = ~0ull;
    {
        const uint64_t gv = resources.getGlobalVersion();
        const uint64_t tv = resources.getTypeVersion<MaterialAsset>();
        if (gv != matTypeCacheGlobal || tv != matTypeCacheType) {
            matTypeCache.clear();
            matTypeCacheGlobal = gv;
            matTypeCacheType   = tv;
        }
    }
    auto materialTypeOf = [&](MaterialHandle h) -> MaterialType {
        if (!h) return MaterialType::Opaque;
        const uint32_t id = h.id();
        if (id >= matTypeCache.size()) matTypeCache.resize(id + 1, 0);
        uint8_t& slot = matTypeCache[id];
        if (slot == 0) slot = static_cast<uint8_t>(resources.get(h).type) + 1u;
        return static_cast<MaterialType>(slot - 1u);
    };

    // Per-instance LOD reads an optional MeshLOD; hoist the storage once (most
    // entities have none, so this is a single contains() probe each).
    const auto* lodStorage = scene.storage<MeshLOD>();

    // Guard against stale EntityIds (entity deleted between visibility and render).
    for (const auto& entry : visibility.entries) {
        if (!scene.isAlive(entry.id)) continue;
        const auto& mesh = scene.get<Mesh>(entry.id);
        if (!mesh.mesh || !mesh.material) continue;  // unresolved slot - skip

        DrawableData drawable;
        // LOD: an entity with a MeshLOD renders the level matching its
        // on-screen size; otherwise the single Mesh::mesh. Shadow casters
        // (gathered below) keep Mesh::mesh regardless, so their cache is stable.
        drawable.mesh = (lodStorage && lodStorage->contains(entry.id.index))
            ? selectLOD(lodStorage->get(entry.id.index), entry.worldMin, entry.worldMax,
                        camera, viewportHeight)
            : mesh.mesh;
        drawable.material     = mesh.material;
        drawable.materialType = materialTypeOf(mesh.material);
        drawable.castShadows  = mesh.castShadows;
        drawable.model        = entry.model;
        drawable.worldMin     = entry.worldMin;
        drawable.worldMax     = entry.worldMax;
        drawable.selected     = scene.has<Selected>(entry.id);
        drawables.emplace_back(drawable);
    }

    // Sort drawables for optimal batching, then sub-sort just the
    // transparent run back-to-front (so blending order is correct and the
    // per-batch refraction snapshot in the transparent forward phase shows
    // panes-behind-panes correctly refracted through).
    sortDrawables(drawables);
    sortTransparentsByDepth(drawables, camera.position);

    // Gather lights (dense iteration, no holes).
    //
    // Done BEFORE the shadow-caster gather (it used to follow it) so the slot
    // pass below tells us whether any light actually casts a shadow this
    // frame. When none does, the full-scene shadow-caster walk + sort is
    // skipped entirely - see the gather block further down.
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
        lightData.areaWidth  = light.areaWidth;
        lightData.areaHeight = light.areaHeight;
        lightData.areaRadius = light.areaRadius;
        lightData.twoSided   = light.twoSided;

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
            if (takenCube < Config::MAX_SHADOW_CASTERS_CUBE) light.shadowSlot = static_cast<int>(takenCube++);
        } else if (light.type == LightType::Directional) {
            // The first directional caster owns the cascade block (NUM_CASCADES
            // consecutive 2D layers); shadowSlot is the base layer. Additional
            // directional casters get no shadow (kept simple - one sun).
            if (!csmAssigned && taken2D + Config::NUM_CASCADES <= Config::MAX_SHADOW_CASTERS_2D) {
                light.shadowSlot = static_cast<int>(taken2D);
                taken2D += Config::NUM_CASCADES;
                csmAssigned = true;
            }
        } else { // Spot, Rect, Disk - 2D atlas shadow
            // Area lights (Rect/Disk) cast point-style shadows from their
            // centre; soft penumbra will arrive with LTC shading.
            if (taken2D < Config::MAX_SHADOW_CASTERS_2D) light.shadowSlot = static_cast<int>(taken2D++);
        }
    }

    // Shadow casters: every visible, shadow-casting, opaque mesh in the scene,
    // independent of the camera frustum (off-screen occluders still cast into
    // view - frustum-culling these is what made Sponza's shadows flicker on
    // view changes). The caster SET and its sort order are matrix-independent,
    // so they change only on structural edits; buildShadowCasters keeps the
    // sorted set in a persistent cache and rebuilds it only when a cheap
    // per-frame fingerprint changes, refreshing matrices live each frame
    // (CODE_REVIEW.md #23).
    //
    // Gated on a shadow slot having actually been assigned above: with no
    // shadow-casting light, shadowCasters stays empty (cleared at the top of
    // build). The cache is deliberately left intact in that case - the
    // fingerprint re-check catches any edit made while shadows were off the
    // next frame a slot is assigned.
    const bool anyShadowSlot = (taken2D > 0u) || (takenCube > 0u);
    if (anyShadowSlot) {
        buildShadowCasters(shadowCasters, shadowCache, scene, resources, materialTypeOf);
    }

    // Reflection probes: snapshot every probe entity plus its world position.
    // The forward pass blends the K nearest by distance falloff. Ordering
    // here is iteration order; the backend / pass picks K = 4 nearest for
    // each fragment so absolute ordering doesn't matter.
    probes.clear();
    probes.reserve(scene.count<ReflectionProbe>());
    scene.forEach<ReflectionProbe, Transform>([&](EntityId id,
            const ReflectionProbe& probe, const Transform& transform) {
        ProbeData pd;
        if (scene.has<WorldTransform>(id)) {
            pd.position = glm::vec3(scene.get<WorldTransform>(id).model[3]);
        } else {
            pd.position = transform.position;
        }
        pd.radius       = probe.radius;
        pd.falloffRange = probe.falloffRange;
        pd.intensity    = probe.intensity;
        pd.hdrPath      = probe.hdrPath;
        pd.bakeVersion  = probe.bakeVersion;
        pd.entityId     = id.index;
        probes.emplace_back(std::move(pd));
    });
}

} // namespace Engine
