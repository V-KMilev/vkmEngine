#define VKM_LOG_CATEGORY "IO"

#include "io/asset/asset_serializer.h"

#include <array>
#include <filesystem>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "logger.h"

#include "ecs/component/animator.h"
#include "ecs/component/decal.h"
#include "ecs/component/lod.h"
#include "ecs/component/mesh.h"
#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "io/asset/asset_cook.h"
#include "io/asset/asset_factory.h"
#include "io/asset/asset_library.h"
#include "io/json_file.h"
#include "io/json_vec.h"
#include "core/reflect.h"

namespace Vkm::Engine {

namespace AssetSerializer {

namespace {

/**
 * @brief Texture fields on MaterialAsset, paired with their stable JSON key.
 * Used by both save (collect referenced textures, emit handle names) and
 * load (the "inline" material factory resolves these refs by name).
 */
struct TexField {
    const char* key;
    TextureHandle MaterialAsset::* member;
};
constexpr std::array<TexField, 11> MATERIAL_TEXTURE_FIELDS = {{
    {"albedo",              &MaterialAsset::albedoTexture},
    {"normal",              &MaterialAsset::normalTexture},
    {"metallicRoughness",   &MaterialAsset::metallicRoughnessTexture},
    {"metallic",            &MaterialAsset::metallicTexture},
    {"roughness",           &MaterialAsset::roughnessTexture},
    {"ao",                  &MaterialAsset::aoTexture},
    {"aoMetallicRoughness", &MaterialAsset::aoMetallicRoughnessTexture},
    {"emission",            &MaterialAsset::emissionTexture},
    {"height",              &MaterialAsset::heightTexture},
    {"clearcoat",           &MaterialAsset::clearcoatTexture},
    {"transmission",        &MaterialAsset::transmissionTexture},
}};

using ::Vkm::Engine::detail::vec3ToJson;
using ::Vkm::Engine::detail::vec4ToJson;
using ::Vkm::Engine::detail::jsonToVec3;
using ::Vkm::Engine::detail::jsonToVec4;

} // namespace

nlohmann::json materialToInline(const MaterialAsset& m, const ResourceManager& resources) {
    nlohmann::json src;
    src["kind"] = "inline";

    // Scalar / vector / enum fields are driven by reflection (the VKM_REFLECT
    // block in resource/asset/material_asset.h), so adding a MaterialAsset field
    // can't silently fall out of the save/load round trip. Texture refs resolve
    // by name instead, below.
    Reflect::forEachField(m, [&](std::string_view name, const auto& val) {
        using V = std::decay_t<decltype(val)>;
        if      constexpr (std::is_same_v<V, MaterialType>) src[std::string(name)] = Reflect::enumName(val);
        else if constexpr (std::is_same_v<V, glm::vec3>)    src[std::string(name)] = vec3ToJson(val);
        else if constexpr (std::is_same_v<V, glm::vec4>)    src[std::string(name)] = vec4ToJson(val);
        else                                                src[std::string(name)] = val;
    });

    nlohmann::json textures = nlohmann::json::object();
    for (const auto& f : MATERIAL_TEXTURE_FIELDS) {
        const TextureHandle& h = m.*f.member;
        if (!h) continue;
        const auto& tex = resources.get(h);
        // Same rule as emitDescriptor, and warned about here rather than left
        // to fire on every load: a hidden texture is not in the cooked
        // manifest, so shipping its name in a public material's recipe writes
        // a reference that can never resolve.
        if (tex.hidden) {
            LOG_WARNING("Material texture slot '%s' refers to hidden asset '%s' - dropping ref",
                f.key, tex.name.c_str());
            continue;
        }
        if (tex.name.empty()) {
            LOG_WARNING("Material texture slot '%s' has no name - dropping ref", f.key);
            continue;
        }
        textures[f.key] = tex.name;
    }
    if (!textures.empty()) src["textures"] = std::move(textures);
    return src;
}

/**
 * @brief Apply an "inline" material descriptor to an existing MaterialAsset,
 * resolving texture refs via findByName.
 *
 * Public (declared in asset_serializer.h) so asset_registration.cpp can hand it
 * to the inline material factory.
 */
void applyInline(const nlohmann::json& src, MaterialAsset& m, const ResourceManager& resources) {
    // Mirror of materialToInline: reflection drives the scalar / vector / enum
    // fields (a missing key keeps the current value, except type which resets
    // to Opaque), textures resolve by name.
    Reflect::forEachField(m, [&](std::string_view name, auto& val) {
        using V = std::decay_t<decltype(val)>;
        const std::string key(name);
        if      constexpr (std::is_same_v<V, MaterialType>) val = Reflect::enumFromName<MaterialType>(src.value(key, std::string("Opaque")));
        else if constexpr (std::is_same_v<V, glm::vec3>)    val = jsonToVec3(src.value(key, nlohmann::json{}), val);
        else if constexpr (std::is_same_v<V, glm::vec4>)    val = jsonToVec4(src.value(key, nlohmann::json{}), val);
        else                                                val = src.value(key, val);
    });

    if (src.contains("textures") && src["textures"].is_object()) {
        for (const auto& f : MATERIAL_TEXTURE_FIELDS) {
            if (!src["textures"].contains(f.key)) continue;
            const std::string texName = src["textures"][f.key].get<std::string>();
            if (texName.empty()) continue;
            const TextureHandle h = resources.findByName<TextureAsset>(texName);
            if (!h) {
                // Keep whatever was already in the slot rather than zeroing
                // it: the file referenced a name that didn't resolve in the
                // current asset graph (typo, dependency not loaded yet, or a
                // texture deleted under us). Silently dropping to null would
                // give the material a transparent-black map at draw time.
                LOG_WARNING("Material texture ref '%s' ('%s') unresolved; keeping previous slot value",
                    f.key, texName.c_str());
                continue;
            }
            m.*f.member = h;
        }
    }
}

namespace {

/**
 * @brief Emit one name-only asset reference into @p target.
 *
 * The asset's data lives in the cooked library (keyed by name); the scene only
 * records the reference. Two kinds of asset cannot be referenced at all and are
 * skipped here, so that every emitter is held to the rule rather than each
 * remembering it: an unnamed one has no key, and a hidden one (editor preview
 * primitives, thumbnail materials) is deliberately absent from the cooked
 * manifest, so its name would name nothing on load.
 */
void emitDescriptor(nlohmann::json& target, const Resource& asset) {
    if (asset.hidden) return;
    if (asset.name.empty()) {
        LOG_WARNING("Asset has no name; skipping in save");
        return;
    }
    target.push_back({{"name", asset.name}});
}

} // namespace

nlohmann::json saveAssetsForEntities(const Scene& scene, const std::vector<EntityId>& entities,
                                     const ResourceManager& resources) {
    nlohmann::json meshes    = nlohmann::json::array();
    nlohmann::json textures  = nlohmann::json::array();
    nlohmann::json materials = nlohmann::json::array();
    nlohmann::json skeletons = nlohmann::json::array();
    nlohmann::json clips     = nlohmann::json::array();

    std::unordered_set<uint32_t> seenMeshes;
    std::unordered_set<uint32_t> seenMaterials;
    std::unordered_set<uint32_t> seenTextures;
    std::unordered_set<uint32_t> seenSkeletons;
    std::unordered_set<uint32_t> seenClips;

    auto emitTexture = [&](const TextureHandle& h) {
        if (!h) return;
        if (!seenTextures.insert(h.id()).second) return;
        emitDescriptor(textures, resources.get(h));
    };

    auto emitMesh = [&](const MeshHandle& h) {
        if (!h || !seenMeshes.insert(h.id()).second) return;
        emitDescriptor(meshes, resources.get(h));
    };

    auto emitMaterial = [&](const MaterialHandle& h) {
        if (!h || !seenMaterials.insert(h.id()).second) return;
        const auto& asset = resources.get(h);
        // Checked here and not left to emitDescriptor because hidden gates the
        // texture walk below too: a thumbnail material must not drag its
        // textures into the user's scene.
        if (asset.hidden) return;
        // Name-only reference; the cooker has already written the material's
        // canonical inline form to the library under this name.
        emitDescriptor(materials, asset);
        // Pull the material's textures into the reference list too, so the
        // loader recreates them first.
        for (const auto& f : MATERIAL_TEXTURE_FIELDS) emitTexture(asset.*f.member);
    };

    auto emitSkeleton = [&](const SkeletonHandle& h) {
        if (!h || !seenSkeletons.insert(h.id()).second) return;
        emitDescriptor(skeletons, resources.get(h));
    };

    auto emitClip = [&](const AnimationClipHandle& h) {
        if (!h || !seenClips.insert(h.id()).second) return;
        emitDescriptor(clips, resources.get(h));
    };

    // Every component that writes an asset name into the document has to be
    // walked here: a name the assets block never lists is a name loadAssets
    // never recreates, and the component's reference resolves to nothing.
    for (EntityId id : entities) {
        if (scene.has<Mesh>(id)) {
            const Mesh& m = scene.get<Mesh>(id);
            emitMesh(m.mesh);
            emitMaterial(m.material);
        }
        if (scene.has<LOD>(id)) {
            for (const LODLevel& level : scene.get<LOD>(id).levels) emitMesh(level.mesh);
        }
        if (scene.has<Decal>(id)) emitMaterial(scene.get<Decal>(id).material);
        if (scene.has<Animator>(id)) {
            const Animator& a = scene.get<Animator>(id);
            emitSkeleton(a.skeleton);
            emitClip(a.clip);
        }
    }

    nlohmann::json out;
    out["textures"]  = std::move(textures);
    out["meshes"]    = std::move(meshes);
    out["materials"] = std::move(materials);
    out["skeletons"] = std::move(skeletons);
    out["clips"]     = std::move(clips);
    return out;
}

nlohmann::json saveAssetsForScene(const Scene& scene, const ResourceManager& resources) {
    // Including the entities inside prefab instances, which the scene file does
    // not describe and the prefab file now carries its own block for. They stay
    // because an instance may override a Mesh or a Decal at an asset the prefab
    // never names, and this walk is the only one that sees that.
    std::vector<EntityId> entities;
    entities.reserve(scene.entityCount());
    scene.forEachEntity([&](EntityId id) { entities.push_back(id); });
    return saveAssetsForEntities(scene, entities, resources);
}

namespace {

/**
 * @brief Load the `source` object from a library recipe file (a material's inline
 * form). Returns false (logging) if the file is missing or malformed.
 */
bool loadLibrarySource(AssetType type, const std::string& name, nlohmann::json& outSource) {
    const std::filesystem::path path = AssetLibrary::recipePath(type, name);
    nlohmann::json doc;
    if (!detail::readJsonFile(path, doc, "Asset library recipe")) return false;
    if (!doc.contains("source")) {
        LOG_ERROR("Asset library recipe %s has no 'source'", path.string().c_str());
        return false;
    }
    outSource = doc["source"];
    return true;
}

/**
 * @brief Resolve a name-only asset reference to the source its factory needs: the
 * cooked binary when the cache can still serve it, otherwise the recipe it was
 * baked from. Returns false if the name is not in the manifest.
 *
 * The cooked file is a cache and the recipe is the source of truth, so the cache
 * is asked first and the recipe answers whenever it cannot. That order is what
 * makes `cooked/` regenerable in practice rather than only on paper: a build
 * that bumped a format version reads the recipe instead, the cooker bakes a
 * current binary from it, and the project repairs itself. Resolving straight to
 * the cooked file would leave the recipe written and never read, and the only
 * way back would be re-importing the source art by hand.
 */
bool resolveCookedSource(AssetType type, const std::string& name, nlohmann::json& outSource) {
    const AssetRecord* record = AssetLibrary::get().find(type, name);
    if (!record) {
        LOG_ERROR("Asset '%s' (%s) has no entry in the asset library manifest",
            name.c_str(), Reflect::enumName(type));
        return false;
    }
    // A material has no cooked binary to prefer - its recipe is its runtime form.
    if (type == AssetType::Material) {
        return loadLibrarySource(type, name, outSource);
    }
    if (AssetCook::isCookedCurrent(type, AssetLibrary::cookedPath(type, name), record->recipeHash)) {
        outSource = nlohmann::json{{"kind", "cooked"}, {"name", name}};
        return true;
    }
    // Not an error on its own: whether it can be recovered from is the factory's
    // answer to give. An editor or a cook re-imports and re-bakes; the runtime,
    // which links no importers, refuses the recipe kind on the next line and
    // that pair of lines is the diagnosis - a shipped build cannot rebuild a
    // stale cache, it needs one cooked for it.
    LOG_INFO("%s '%s': no cooked file this build can use; falling back to its recipe",
        Reflect::enumName(type), name.c_str());
    return loadLibrarySource(type, name, outSource);
}

/**
 * @brief Recreate one asset section (textures / materials / meshes) from its
 * JSON array, dispatching each name-only reference through @p factory and
 * renaming the result to the recorded name. Returns {created, already-present}.
 */
template<typename Asset>
std::pair<size_t, size_t> loadAssetSection(
    const nlohmann::json& assetsJson, const char* sectionKey, AssetType type,
    Handle<Asset> (*factory)(const nlohmann::json&, ResourceManager&),
    const char* what, ResourceManager& resources) {
    size_t created = 0, skipped = 0;
    auto it = assetsJson.find(sectionKey);
    if (it == assetsJson.end() || !it->is_array() || it->empty()) return {created, skipped};

    // Loop-invariant: a null dispatch means this binary was built without the
    // factory wired (e.g. the runtime with no recipe importers). Bail once
    // rather than re-checking and logging per entry.
    if (!factory) {
        LOG_ERROR("No %s dispatch wired (misconfigured binary?) - skipping section", what);
        return {created, skipped};
    }

    for (const auto& entry : *it) {
        const std::string name = entry.value("name", std::string{});
        if (name.empty()) {
            LOG_WARNING("%s entry missing 'name' - skipping", what);
            continue;
        }
        if (resources.findByName<Asset>(name)) { ++skipped; continue; }

        nlohmann::json source;
        if (!resolveCookedSource(type, name, source)) continue;
        Handle<Asset> h = factory(source, resources);
        if (!h) {
            LOG_WARNING("%s '%s' could not be recreated - skipping", what, name.c_str());
            continue;
        }
        resources.rename(h, name);
        ++created;
    }
    return {created, skipped};
}

} // namespace

bool loadAssets(const nlohmann::json& assetsJson, ResourceManager& resources) {
    if (!assetsJson.is_object()) {
        LOG_WARNING("Assets block is not an object - skipping");
        return false;
    }

    // Order matters: textures -> materials (resolve their texture refs by name)
    // -> skeletons -> clips (each names the rig its bone indices address) ->
    // meshes. Each created asset is renamed to its recorded name so component
    // references (which resolve by name) land on it.
    const auto [texC, texS] = loadAssetSection<TextureAsset      >(assetsJson, "textures",  AssetType::Texture,       assetFactory().createTexture,       "Texture",  resources);
    const auto [matC, matS] = loadAssetSection<MaterialAsset     >(assetsJson, "materials", AssetType::Material,      assetFactory().createMaterial,      "Material", resources);
    const auto [sklC, sklS] = loadAssetSection<SkeletonAsset     >(assetsJson, "skeletons", AssetType::Skeleton,      assetFactory().createSkeleton,      "Skeleton", resources);
    const auto [clpC, clpS] = loadAssetSection<AnimationClipAsset>(assetsJson, "clips",     AssetType::AnimationClip, assetFactory().createAnimationClip, "Clip",     resources);
    const auto [mshC, mshS] = loadAssetSection<MeshAsset         >(assetsJson, "meshes",    AssetType::Mesh,          assetFactory().createMesh,          "Mesh",     resources);

    // Silent when the block asked for nothing new: a prefab carries its own
    // assets and is instantiated once per instance, per scene load, per
    // duplicate and per undo of one.
    if (texC + matC + sklC + clpC + mshC > 0) {
        LOG_INFO("%zu texture(s), %zu material(s), %zu rig(s), %zu clip(s), %zu mesh(es) created; "
            "%zu+%zu+%zu+%zu+%zu skipped (already loaded)",
            texC, matC, sklC, clpC, mshC, texS, matS, sklS, clpS, mshS);
    }
    return true;
}

} // namespace AssetSerializer

} // namespace Vkm::Engine
