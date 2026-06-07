#define VKM_LOG_CATEGORY "RESOURCE"

#include "resource/asset_gc.h"

#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "logger.h"

#include "ecs/scene.h"
#include "ecs/component/mesh.h"
#include "ecs/component/mesh_lod.h"
#include "resource/resource_manager.h"
#include "resource/mesh_asset.h"
#include "resource/material_asset.h"
#include "resource/texture_asset.h"

namespace Engine {

namespace {

// Pointer-to-member list of every texture slot on a material. Mirrors the
// handle fields in material_asset.h - kept local so the GC doesn't reach into
// the serializer's private MATERIAL_TEXTURE_FIELDS. If a texture slot is added
// to MaterialAsset, add it here too (otherwise that texture looks unreferenced
// and gets swept).
constexpr std::array<TextureHandle MaterialAsset::*, 11> MATERIAL_TEXTURES = {
    &MaterialAsset::albedoTexture,
    &MaterialAsset::emissionTexture,
    &MaterialAsset::roughnessTexture,
    &MaterialAsset::metallicTexture,
    &MaterialAsset::normalTexture,
    &MaterialAsset::aoTexture,
    &MaterialAsset::heightTexture,
    &MaterialAsset::clearcoatTexture,
    &MaterialAsset::transmissionTexture,
    &MaterialAsset::metallicRoughnessTexture,
    &MaterialAsset::aoMetallicRoughnessTexture,
};

// Engine-owned (pinned) or editor-hidden assets always survive a sweep.
inline bool survivesUnconditionally(const Resource& r) { return r.pinned || r.hidden; }

} // namespace

std::size_t purgeUnusedAssets(Scene& scene, ResourceManager& resources) {
    // Mark phase: ids reachable from the live scene. Handle::id() is the
    // per-type slot index, unique within its type, so a set per type suffices.
    std::unordered_set<uint32_t> refMeshes;
    std::unordered_set<uint32_t> refMaterials;
    std::unordered_set<uint32_t> refTextures;

    scene.forEach<Mesh>([&](EntityId, const Mesh& m) {
        if (m.mesh)     refMeshes.insert(m.mesh.id());
        if (m.material) refMaterials.insert(m.material.id());
    });
    scene.forEach<MeshLOD>([&](EntityId, const MeshLOD& lod) {
        for (int i = 0; i < lod.count && i < MeshLOD::MAX_LEVELS; ++i) {
            if (lod.levels[i]) refMeshes.insert(lod.levels[i].id());
        }
    });

    // A texture is reachable only through a material that itself survives
    // (scene-referenced or pinned/hidden). Pull those materials' texture refs
    // into the keep set so we don't sweep a texture still in use.
    resources.forEachOfType<MaterialAsset>([&](MaterialHandle h, const MaterialAsset& mat) {
        const bool survives = survivesUnconditionally(mat) || refMaterials.count(h.id());
        if (!survives) return;
        for (const auto member : MATERIAL_TEXTURES) {
            const TextureHandle& t = mat.*member;
            if (t) refTextures.insert(t.id());
        }
    });

    // Sweep phase: collect first, remove after - remove() swap-pops the dense
    // array, so erasing mid-iteration would skip elements.
    std::vector<MeshHandle>     deadMeshes;
    std::vector<MaterialHandle> deadMaterials;
    std::vector<TextureHandle>  deadTextures;

    resources.forEachOfType<MeshAsset>([&](MeshHandle h, const MeshAsset& a) {
        if (!survivesUnconditionally(a) && !refMeshes.count(h.id())) deadMeshes.push_back(h);
    });
    resources.forEachOfType<MaterialAsset>([&](MaterialHandle h, const MaterialAsset& a) {
        if (!survivesUnconditionally(a) && !refMaterials.count(h.id())) deadMaterials.push_back(h);
    });
    resources.forEachOfType<TextureAsset>([&](TextureHandle h, const TextureAsset& a) {
        if (!survivesUnconditionally(a) && !refTextures.count(h.id())) deadTextures.push_back(h);
    });

    for (const MeshHandle h : deadMeshes)        resources.remove(h);
    for (const MaterialHandle h : deadMaterials) resources.remove(h);
    for (const TextureHandle h : deadTextures)   resources.remove(h);

    const std::size_t total = deadMeshes.size() + deadMaterials.size() + deadTextures.size();
    if (total > 0) {
        LOG_INFO("Purged %zu unused asset(s): %zu mesh(es), %zu material(s), %zu texture(s)",
            total, deadMeshes.size(), deadMaterials.size(), deadTextures.size());
    } else {
        LOG_INFO("Purge unused: nothing to free (every asset is reachable or pinned)");
    }
    return total;
}

} // namespace Engine
