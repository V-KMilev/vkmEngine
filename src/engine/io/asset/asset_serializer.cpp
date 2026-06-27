#define VKM_LOG_CATEGORY "IO"

#include "io/asset/asset_serializer.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <unordered_set>

#include "logger.h"

#include "ecs/component/mesh.h"
#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "io/asset/asset_factory.h"
#include "io/asset/asset_library.h"
#include "io/json_vec.h"
#include "io/project_paths.h"
#include "core/reflect.h"

namespace Engine {

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

// vec/quat <-> JSON helpers are shared with component_serializer; see io/json_vec.h.
using ::Engine::detail::vec3ToJson;
using ::Engine::detail::vec4ToJson;
using ::Engine::detail::jsonToVec3;
using ::Engine::detail::jsonToVec4;

} // namespace

nlohmann::json materialToInline(const MaterialAsset& m, const ResourceManager& resources) {
    nlohmann::json src;
    src["kind"] = "inline";

    // Scalar / vector / enum fields are driven by reflection (the VKM_REFLECT
    // block at the bottom of this file), so adding a MaterialAsset field can't
    // silently fall out of the save/load round trip. Texture refs are special
    // (resolved by name) and handled below.
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
        const std::string& texName = resources.get(h).name;
        if (texName.empty()) {
            LOG_WARNING("Material texture slot '%s' has no name - dropping ref", f.key);
            continue;
        }
        textures[f.key] = texName;
    }
    if (!textures.empty()) src["textures"] = std::move(textures);
    return src;
}

namespace {

/**
 * @brief Apply an "inline" material descriptor to an existing MaterialAsset,
 * resolving texture refs via findByName.
 */
void applyInlineMaterial(const nlohmann::json& src, MaterialAsset& m, const ResourceManager& resources) {
    // Mirror of materialToInline: reflection drives the scalar / vector / enum
    // fields (a missing key keeps the current value), textures resolve by name.
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

/**
 * @brief Emit one name-only asset reference into @p target.
 *
 * The asset's data lives in the cooked library (keyed by name); the scene only
 * records the reference. An unnamed asset can't be referenced, so it is skipped.
 */
void emitDescriptor(nlohmann::json& target, const Resource& asset) {
    if (asset.name.empty()) {
        LOG_WARNING("Asset has no name; skipping in save");
        return;
    }
    target.push_back({{"name", asset.name}});
}

} // namespace

nlohmann::json saveAssetsForScene(const Scene& scene, const ResourceManager& resources) {
    nlohmann::json meshes    = nlohmann::json::array();
    nlohmann::json textures  = nlohmann::json::array();
    nlohmann::json materials = nlohmann::json::array();

    std::unordered_set<uint32_t> seenMeshes;
    std::unordered_set<uint32_t> seenMaterials;
    std::unordered_set<uint32_t> seenTextures;

    auto emitTexture = [&](const TextureHandle& h) {
        if (!h) return;
        if (!seenTextures.insert(h.id()).second) return;
        emitDescriptor(textures, resources.get(h));
    };

    scene.forEach<Mesh>([&](EntityId, const Mesh& m) {
        if (m.mesh && seenMeshes.insert(m.mesh.id()).second) {
            const auto& asset = resources.get(m.mesh);
            // Hidden assets (editor preview primitives etc.) never serialize:
            // they belong to the running editor, not to the user's scene.
            if (!asset.hidden) emitDescriptor(meshes, asset);
        }
        if (m.material && seenMaterials.insert(m.material.id()).second) {
            const auto& asset = resources.get(m.material);
            if (asset.hidden) return;
            // Name-only reference; the cooker has already written the material's
            // canonical inline form to the library under this name.
            emitDescriptor(materials, asset);
            // Pull every texture this material references into the texture
            // reference list too, so the loader cooks/loads them first.
            for (const auto& f : MATERIAL_TEXTURE_FIELDS) emitTexture(asset.*f.member);
        }
    });

    nlohmann::json out;
    out["textures"]  = std::move(textures);
    out["meshes"]    = std::move(meshes);
    out["materials"] = std::move(materials);
    return out;
}

namespace {

/**
 * @brief Load the `source` object from a library recipe file (a material's inline
 * form). Returns false (logging) if the file is missing or malformed.
 */
bool loadLibrarySource(const Record& record, nlohmann::json& outSource) {
    const std::filesystem::path path = AssetLibrary::get().recipePath(record);
    std::ifstream in(path);
    if (!in) {
        LOG_ERROR("Asset library recipe missing: %s", path.string().c_str());
        return false;
    }
    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("Asset library recipe malformed %s: %s", path.string().c_str(), e.what());
        return false;
    }
    if (!doc.contains("source")) {
        LOG_ERROR("Asset library recipe %s has no 'source'", path.string().c_str());
        return false;
    }
    outSource = doc["source"];
    return true;
}

/**
 * @brief Resolve a name-only asset reference to the source its factory needs: meshes
 * and textures load from the cooked cache by name; materials load their inline
 * descriptor from the library file. Returns false if the name is not in the
 * manifest.
 */
bool resolveCookedSource(AssetType type, const std::string& name, nlohmann::json& outSource) {
    const Record* record = AssetLibrary::get().find(type, name);
    if (!record) {
        LOG_ERROR("Asset '%s' (%s) has no entry in the asset library manifest",
            name.c_str(), Reflect::enumName(type));
        return false;
    }
    if (type == AssetType::Material) {
        return loadLibrarySource(*record, outSource);
    }
    outSource = nlohmann::json{{"kind", "cooked"}, {"name", name}};
    return true;
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
    if (it == assetsJson.end() || !it->is_array()) return {created, skipped};

    for (const auto& entry : *it) {
        const std::string name = entry.value("name", std::string{});
        if (name.empty()) {
            LOG_WARNING("%s entry missing 'name' - skipping", what);
            continue;
        }
        if (resources.findByName<Asset>(name)) { ++skipped; continue; }

        nlohmann::json source;
        if (!resolveCookedSource(type, name, source)) continue;
        if (!factory) {
            LOG_ERROR("No %s dispatch wired (misconfigured binary?) - skipping '%s'", what, name.c_str());
            continue;
        }
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
    // -> meshes. Each created asset is renamed to its recorded name so component
    // references (which resolve by name) land on it.
    const auto [texC, texS] = loadAssetSection<TextureAsset >(assetsJson, "textures",  AssetType::Texture,  assetFactory().createTexture,  "Texture",  resources);
    const auto [matC, matS] = loadAssetSection<MaterialAsset>(assetsJson, "materials", AssetType::Material, assetFactory().createMaterial, "Material", resources);
    const auto [mshC, mshS] = loadAssetSection<MeshAsset    >(assetsJson, "meshes",    AssetType::Mesh,     assetFactory().createMesh,     "Mesh",     resources);

    LOG_INFO("%zu texture(s), %zu material(s), %zu mesh(es) created; %zu+%zu+%zu skipped (already loaded)",
        texC, matC, mshC, texS, matS, mshS);
    return true;
}

// Expose the inline applier for asset_registration.cpp to use when
// registering the inline material factory.
void applyInline(const nlohmann::json& src, MaterialAsset& m, const ResourceManager& resources) {
    applyInlineMaterial(src, m, resources);
}

} // namespace AssetSerializer

} // namespace Engine
