#define VKM_LOG_CATEGORY "IO"

#include "io/asset_serializer.h"

#include <array>
#include <string>
#include <unordered_set>

#include "logger.h"

#include "ecs/component/mesh.h"
#include "ecs/component/mesh_lod.h"
#include "ecs/scene.h"
#include "resource/asset_database.h"
#include "resource/asset_id.h"
#include "resource/resource_manager.h"
#include "io/reflect.h"

namespace Engine {

AssetFactories& AssetFactories::get() {
    static AssetFactories instance;
    return instance;
}

void AssetFactories::registerMesh(std::string kind, MeshFactory factory) {
    m_meshFactories[std::move(kind)] = std::move(factory);
}

void AssetFactories::registerTexture(std::string kind, TextureFactory factory) {
    m_textureFactories[std::move(kind)] = std::move(factory);
}

void AssetFactories::registerMaterial(std::string kind, MaterialFactory factory) {
    m_materialFactories[std::move(kind)] = std::move(factory);
}

void AssetFactories::registerShader(std::string kind, ShaderFactory factory) {
    m_shaderFactories[std::move(kind)] = std::move(factory);
}

MeshHandle AssetFactories::createMesh(const nlohmann::json& source, ResourceManager& resources) const {
    const std::string kind = source.value("kind", std::string{});
    auto it = m_meshFactories.find(kind);
    if (it == m_meshFactories.end()) {
        LOG_ERROR("No mesh factory registered for kind '%s' (typo? unsupported builtin? unregistered user factory?)", kind.c_str());
        return {};
    }
    return it->second(source, resources);
}

TextureHandle AssetFactories::createTexture(const nlohmann::json& source, ResourceManager& resources) const {
    const std::string kind = source.value("kind", std::string{});
    auto it = m_textureFactories.find(kind);
    if (it == m_textureFactories.end()) {
        LOG_ERROR("No texture factory registered for kind '%s' (typo? unsupported builtin? unregistered user factory?)", kind.c_str());
        return {};
    }
    return it->second(source, resources);
}

MaterialHandle AssetFactories::createMaterial(const nlohmann::json& source, ResourceManager& resources) const {
    const std::string kind = source.value("kind", std::string{});
    auto it = m_materialFactories.find(kind);
    if (it == m_materialFactories.end()) {
        LOG_ERROR("No material factory registered for kind '%s' (typo? unsupported builtin? unregistered user factory?)", kind.c_str());
        return {};
    }
    return it->second(source, resources);
}

ShaderAsset AssetFactories::createShader(const nlohmann::json& source) const {
    const std::string kind = source.value("kind", std::string{});
    auto it = m_shaderFactories.find(kind);
    if (it == m_shaderFactories.end()) {
        LOG_ERROR("No shader factory registered for kind '%s' (typo? unsupported builtin? unregistered user factory?)", kind.c_str());
        return {};
    }
    return it->second(source);
}

namespace AssetSerializer {

namespace {

/// Texture fields on MaterialAsset, paired with their stable JSON key.
/// Used by both save (collect referenced textures, emit handle names) and
/// load (the "inline" material factory resolves these refs by name).
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

nlohmann::json vec3ToJson(const glm::vec3& v) { return {v.x, v.y, v.z}; }
nlohmann::json vec4ToJson(const glm::vec4& v) { return {v.x, v.y, v.z, v.w}; }
glm::vec3 vec3FromJson(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {j[0], j[1], j[2]};
}
glm::vec4 vec4FromJson(const nlohmann::json& j, const glm::vec4& fallback) {
    if (!j.is_array() || j.size() < 4) return fallback;
    return {j[0], j[1], j[2], j[3]};
}

/// Build an "inline" material source descriptor capturing all PBR scalars +
/// texture refs by name. This is what we emit on save regardless of how the
/// material was first created - editor tweaks survive cold-start load.
nlohmann::json materialToInline(const MaterialAsset& m, const ResourceManager& resources) {
    nlohmann::json src;
    src["kind"]  = "inline";
    src["type"]  = Reflect::enumName(m.type, MATERIAL_TYPE_NAMES);
    src["albedo"]              = vec4ToJson(m.albedo);
    src["emission"]            = vec3ToJson(m.emission);
    src["metallic"]            = m.metallic;
    src["roughness"]           = m.roughness;
    src["ior"]                 = m.ior;
    src["transmission"]        = m.transmission;
    src["alpha"]               = m.alpha;
    src["ao"]                  = m.ao;
    src["clearcoat"]           = m.clearcoat;
    src["clearcoatRoughness"]  = m.clearcoatRoughness;
    src["anisotropy"]          = m.anisotropy;
    src["anisotropyDirection"] = vec3ToJson(m.anisotropyDirection);
    src["subsurface"]          = m.subsurface;
    src["subsurfaceColor"]     = vec3ToJson(m.subsurfaceColor);
    src["heightScale"]         = m.heightScale;
    src["normalScale"]         = m.normalScale;
    src["thicknessFactor"]     = m.thicknessFactor;
    src["attenuationDistance"] = m.attenuationDistance;
    src["attenuationColor"]    = vec3ToJson(m.attenuationColor);
    src["alphaCutoff"]         = m.alphaCutoff;
    src["useOIT"]              = m.useOIT;

    nlohmann::json textures = nlohmann::json::object();
    for (const auto& f : MATERIAL_TEXTURE_FIELDS) {
        const TextureHandle& h = m.*f.member;
        if (!h) continue;
        const AssetId id = resources.get(h).assetId;
        if (!id) {
            LOG_WARNING("Material texture slot '%s' has no AssetId (%s) - dropping ref",
                f.key, resources.get(h).name.c_str());
            continue;
        }
        textures[f.key] = id.toString();
    }
    if (!textures.empty()) src["textures"] = std::move(textures);
    return src;
}

/// Apply an "inline" material descriptor to an existing MaterialAsset,
/// resolving texture refs via findByName.
void applyInlineMaterial(const nlohmann::json& src, MaterialAsset& m, const ResourceManager& resources) {
    const std::string typeStr = src.value("type", std::string{"Opaque"});
    m.type = Reflect::enumFromName<MaterialType>(typeStr, MATERIAL_TYPE_NAMES);

    m.albedo              = vec4FromJson(src.value("albedo",              nlohmann::json{}), m.albedo);
    m.emission            = vec3FromJson(src.value("emission",            nlohmann::json{}), m.emission);
    m.metallic            = src.value("metallic",            m.metallic);
    m.roughness           = src.value("roughness",           m.roughness);
    m.ior                 = src.value("ior",                 m.ior);
    m.transmission        = src.value("transmission",        m.transmission);
    m.alpha               = src.value("alpha",               m.alpha);
    m.ao                  = src.value("ao",                  m.ao);
    m.clearcoat           = src.value("clearcoat",           m.clearcoat);
    m.clearcoatRoughness  = src.value("clearcoatRoughness",  m.clearcoatRoughness);
    m.anisotropy          = src.value("anisotropy",          m.anisotropy);
    m.anisotropyDirection = vec3FromJson(src.value("anisotropyDirection", nlohmann::json{}), m.anisotropyDirection);
    m.subsurface          = src.value("subsurface",          m.subsurface);
    m.subsurfaceColor     = vec3FromJson(src.value("subsurfaceColor",     nlohmann::json{}), m.subsurfaceColor);
    m.heightScale         = src.value("heightScale",         m.heightScale);
    m.normalScale         = src.value("normalScale",         m.normalScale);
    m.thicknessFactor     = src.value("thicknessFactor",     m.thicknessFactor);
    m.attenuationDistance = src.value("attenuationDistance", m.attenuationDistance);
    m.attenuationColor    = vec3FromJson(src.value("attenuationColor",    nlohmann::json{}), m.attenuationColor);
    m.alphaCutoff         = src.value("alphaCutoff",         m.alphaCutoff);
    m.useOIT              = src.value("useOIT",              m.useOIT);

    if (src.contains("textures") && src["textures"].is_object()) {
        for (const auto& f : MATERIAL_TEXTURE_FIELDS) {
            if (!src["textures"].contains(f.key)) continue;
            const AssetId id = AssetId::fromString(src["textures"][f.key].get<std::string>());
            if (!id) continue;
            const TextureHandle h = resources.findById<TextureAsset>(id);
            if (!h) {
                // Keep whatever was already in the slot rather than zeroing
                // it: the file referenced a GUID that didn't resolve in the
                // current asset graph (typo, dependency not loaded yet, or a
                // texture deleted under us). Silently dropping to null would
                // give the material a transparent-black map at draw time.
                LOG_WARNING("Material texture ref '%s' (%s) unresolved; keeping previous slot value",
                    f.key, id.toString().c_str());
                continue;
            }
            m.*f.member = h;
        }
    }
}

/**
 * @brief Emit one asset descriptor into @p target.
 *
 * Skips assets with no AssetId - those can't be referenced by the scene
 * anyway. @p sourceOverride lets materials substitute their derived
 * `inline` form for the asset's runtime source.
 */
void emitDescriptor(
    nlohmann::json& target,
    const Resource& asset,
    const nlohmann::json* sourceOverride = nullptr
) {
    if (!asset.assetId) {
        LOG_WARNING("Asset '%s' has no AssetId; skipping in save", asset.name.c_str());
        return;
    }
    const nlohmann::json* source = sourceOverride ? sourceOverride : asset.source.get();
    if (!source || source->is_null()) {
        LOG_WARNING("Asset '%s' has no source descriptor; skipping in save", asset.name.c_str());
        return;
    }
    target.push_back({
        {"id",     asset.assetId.toString()},
        {"name",   asset.name},          // Human-readable hint; not load-bearing.
        {"source", *source},
    });
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
            // Materials always save as `inline` - captures the actual runtime
            // state (including editor scalar tweaks) regardless of how the
            // material was originally created.
            nlohmann::json inlineDesc = materialToInline(asset, resources);
            emitDescriptor(materials, asset, &inlineDesc);
            // Pull every texture this material references into the texture
            // descriptor pool too.
            for (const auto& f : MATERIAL_TEXTURE_FIELDS) emitTexture(asset.*f.member);
        }
    });

    // LOD level meshes (levels 1..count-1) are referenced only by MeshLOD, not
    // by any Mesh component, so they would otherwise never be emitted. Walk
    // them here. Level 0 is conventionally the entity's Mesh::mesh and is
    // already in seenMeshes, so the dedup skips it. Decimated levels carry a
    // "decimate" source (see decimateMeshTracked) and re-decimate on load; the
    // Mesh-loop above always emits their base level first, so the base is
    // present in the array before the factory needs it.
    scene.forEach<MeshLOD>([&](EntityId, const MeshLOD& lod) {
        for (int i = 0; i < lod.count && i < MeshLOD::MAX_LEVELS; ++i) {
            if (lod.levels[i] && seenMeshes.insert(lod.levels[i].id()).second) {
                const auto& asset = resources.get(lod.levels[i]);
                if (!asset.hidden) emitDescriptor(meshes, asset);
            }
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
 * @brief Pull the per-section identifying tuple (id, name, source) from
 *        a single JSON entry.
 *
 * Returns false if id/source are missing/malformed - the caller logs +
 * skips. name is non-load-bearing (a human hint).
 */
bool unpackEntry(
    const nlohmann::json& entry,
    AssetId& outId,
    std::string& outName,
    nlohmann::json& outSource
) {
    outId = AssetId::fromString(entry.value("id", std::string{}));
    if (!outId) return false;
    outName = entry.value("name", std::string{});
    outSource = entry.contains("source") ? entry["source"] : nlohmann::json{};
    return true;
}

/// Seed AssetDatabase with the scene's recorded ids so the factory-created
/// assets (which call AssetDatabase::registerOrGet on the descriptor path)
/// land under the same ids the scene already references. Without this,
/// loading on a machine whose DB doesn't yet know these paths would mint
/// fresh ids and break every component reference in the scene.
void seedDatabase(const nlohmann::json& section, AssetKind kind) {
    if (!section.is_array()) return;
    for (const auto& entry : section) {
        const AssetId id = AssetId::fromString(entry.value("id", std::string{}));
        if (!id) continue;
        if (!entry.contains("source") || !entry["source"].is_object()) continue;
        const auto& src = entry["source"];
        if (!src.contains("path") || !src["path"].is_string()) continue;
        AssetDatabase::get().registerWithId(src["path"].get<std::string>(), id, kind);
    }
}

} // namespace

bool loadAssets(const nlohmann::json& assetsJson, ResourceManager& resources) {
    if (!assetsJson.is_object()) {
        LOG_WARNING("Assets block is not an object - skipping");
        return false;
    }

    // Seed the database before any factory runs. Materials use "folder"
    // descriptors with a `path` field; textures use "file" descriptors
    // also keyed `path`; meshes use procedural ("generator"/"model")
    // descriptors whose path field varies - seedDatabase ignores entries
    // without a path field, so non-file-backed meshes are simply minted
    // a fresh GUID later. That mismatch is captured by the inline
    // fallback path below.
    if (assetsJson.contains("textures"))  seedDatabase(assetsJson["textures"],  AssetKind::Texture);
    if (assetsJson.contains("materials")) seedDatabase(assetsJson["materials"], AssetKind::Material);
    if (assetsJson.contains("meshes"))    seedDatabase(assetsJson["meshes"],    AssetKind::Mesh);

    const auto& factories = AssetFactories::get();

    size_t texturesCreated = 0, texturesSkipped = 0;
    size_t materialsCreated = 0, materialsSkipped = 0;
    size_t meshesCreated = 0, meshesSkipped = 0;

    // Helper: ensure the freshly-created asset's assetId matches the scene's
    // recorded one. The loader usually stamps the correct GUID (seeded DB +
    // path-based register), but procedural assets (no on-disk path) get a
    // fresh GUID instead - patch + reindex so scene refs still resolve.
    auto reconcileId = [&resources](auto handle, AssetId expected) {
        if (!handle) return;
        auto& asset = resources.edit(handle);
        if (asset.assetId == expected) return;
        asset.assetId = expected;
        resources.reindexAssetId(handle);
    };

    // Textures first - materials reference them by id. Order matters:
    // textures -> materials -> meshes. Meshes don't depend on the others;
    // they're last only for output stability.
    if (assetsJson.contains("textures") && assetsJson["textures"].is_array()) {
        for (const auto& entry : assetsJson["textures"]) {
            AssetId id; std::string name; nlohmann::json source;
            if (!unpackEntry(entry, id, name, source)) {
                LOG_WARNING("Texture entry missing 'id' - skipping");
                continue;
            }
            if (resources.findById<TextureAsset>(id)) { ++texturesSkipped; continue; }
            TextureHandle h = factories.createTexture(source, resources);
            if (!h) {
                LOG_WARNING("Texture %s could not be recreated - skipping", id.toString().c_str());
                continue;
            }
            reconcileId(h, id);
            ++texturesCreated;
        }
    }

    if (assetsJson.contains("materials") && assetsJson["materials"].is_array()) {
        for (const auto& entry : assetsJson["materials"]) {
            AssetId id; std::string name; nlohmann::json source;
            if (!unpackEntry(entry, id, name, source)) {
                LOG_WARNING("Material entry missing 'id' - skipping");
                continue;
            }
            if (resources.findById<MaterialAsset>(id)) { ++materialsSkipped; continue; }
            MaterialHandle h = factories.createMaterial(source, resources);
            if (!h) {
                LOG_WARNING("Material %s could not be recreated - skipping", id.toString().c_str());
                continue;
            }
            if (!name.empty()) resources.rename(h, name);
            reconcileId(h, id);
            ++materialsCreated;
        }
    }

    if (assetsJson.contains("meshes") && assetsJson["meshes"].is_array()) {
        for (const auto& entry : assetsJson["meshes"]) {
            AssetId id; std::string name; nlohmann::json source;
            if (!unpackEntry(entry, id, name, source)) {
                LOG_WARNING("Mesh entry missing 'id' - skipping");
                continue;
            }
            if (resources.findById<MeshAsset>(id)) { ++meshesSkipped; continue; }
            MeshHandle h = factories.createMesh(source, resources);
            if (!h) {
                LOG_WARNING("Mesh %s could not be recreated - skipping", id.toString().c_str());
                continue;
            }
            // For the "model" factory the GUID is already correct (stamped
            // from the seeded DB). For "generator" the stamp came from the
            // canonical generator key, which may not match the scene id -
            // reconcileId patches both.
            reconcileId(h, id);
            if (!name.empty()) resources.rename(h, name);
            ++meshesCreated;
        }
    }

    LOG_INFO("%zu texture(s), %zu material(s), %zu mesh(es) created; %zu+%zu+%zu skipped (already loaded)",
        texturesCreated, materialsCreated, meshesCreated,
        texturesSkipped, materialsSkipped, meshesSkipped);
    return true;
}

// Expose the inline applier for asset_registration.cpp to use when
// registering the inline material factory.
void applyInline(const nlohmann::json& src, MaterialAsset& m, const ResourceManager& resources) {
    applyInlineMaterial(src, m, resources);
}

} // namespace AssetSerializer

} // namespace Engine
