#include "io/asset_serializer.h"

#include <array>
#include <string>
#include <unordered_set>

#include "logger.h"

#include "ecs/component/mesh.h"
#include "ecs/scene.h"
#include "resource/resource_manager.h"

namespace Engine {

// ---- AssetFactories ------------------------------------------------------
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

MeshAsset AssetFactories::createMesh(const nlohmann::json& source) const {
    const std::string kind = source.value("kind", std::string{});
    auto it = m_meshFactories.find(kind);
    if (it == m_meshFactories.end()) {
        LOG_WARNING("AssetFactories: no mesh factory registered for kind '%s'", kind.c_str());
        return {};
    }
    return it->second(source);
}

TextureHandle AssetFactories::createTexture(const nlohmann::json& source, ResourceManager& resources) const {
    const std::string kind = source.value("kind", std::string{});
    auto it = m_textureFactories.find(kind);
    if (it == m_textureFactories.end()) {
        LOG_WARNING("AssetFactories: no texture factory registered for kind '%s'", kind.c_str());
        return {};
    }
    return it->second(source, resources);
}

MaterialHandle AssetFactories::createMaterial(const nlohmann::json& source, ResourceManager& resources) const {
    const std::string kind = source.value("kind", std::string{});
    auto it = m_materialFactories.find(kind);
    if (it == m_materialFactories.end()) {
        LOG_WARNING("AssetFactories: no material factory registered for kind '%s'", kind.c_str());
        return {};
    }
    return it->second(source, resources);
}

ShaderAsset AssetFactories::createShader(const nlohmann::json& source) const {
    const std::string kind = source.value("kind", std::string{});
    auto it = m_shaderFactories.find(kind);
    if (it == m_shaderFactories.end()) {
        LOG_WARNING("AssetFactories: no shader factory registered for kind '%s'", kind.c_str());
        return {};
    }
    return it->second(source);
}

// ---- AssetSerializer -----------------------------------------------------
namespace AssetSerializer {

namespace {

/// Texture fields on MaterialAsset, paired with their stable JSON key.
/// Used by both save (collect referenced textures, emit handle names) and
/// load (the "inline" material factory resolves these refs by name).
struct TexField {
    const char* key;
    TextureHandle MaterialAsset::* member;
};
constexpr std::array<TexField, 11> kMaterialTextureFields = {{
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
/// material was first created — editor tweaks survive cold-start load.
nlohmann::json materialToInline(const MaterialAsset& m, const ResourceManager& resources) {
    nlohmann::json src;
    src["kind"]  = "inline";
    src["type"]  = (m.type == MaterialType::Transparent) ? "Transparent"
                 : (m.type == MaterialType::Unlit)       ? "Unlit"
                                                         : "Opaque";
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

    nlohmann::json textures = nlohmann::json::object();
    for (const auto& f : kMaterialTextureFields) {
        const TextureHandle& h = m.*f.member;
        if (h) textures[f.key] = resources.get(h).name;
    }
    if (!textures.empty()) src["textures"] = std::move(textures);
    return src;
}

/// Apply an "inline" material descriptor to an existing MaterialAsset,
/// resolving texture refs via findByName.
void applyInlineMaterial(const nlohmann::json& src, MaterialAsset& m, const ResourceManager& resources) {
    const std::string typeStr = src.value("type", std::string{"Opaque"});
    m.type = (typeStr == "Transparent") ? MaterialType::Transparent
           : (typeStr == "Unlit")       ? MaterialType::Unlit
                                        : MaterialType::Opaque;

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

    if (src.contains("textures") && src["textures"].is_object()) {
        for (const auto& f : kMaterialTextureFields) {
            if (!src["textures"].contains(f.key)) continue;
            const std::string texName = src["textures"][f.key].get<std::string>();
            const TextureHandle h = resources.findByName<TextureAsset>(texName);
            if (!h) {
                LOG_WARNING("AssetSerializer: material texture ref '%s' (%s) unresolved",
                    f.key, texName.c_str());
            }
            m.*f.member = h;
        }
    }
}

/// Emit one asset descriptor. Skips assets with no source — they can't be
/// recreated at load time and would round-trip into nothing.
void emitDescriptor(nlohmann::json& target, const std::string& name, const nlohmann::json& source) {
    if (source.is_null()) {
        LOG_WARNING("AssetSerializer: asset '%s' has no source; skipping in save", name.c_str());
        return;
    }
    target.push_back({{"name", name}, {"source", source}});
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
        const auto& asset = resources.get(h);
        emitDescriptor(textures, asset.name, asset.source);
    };

    scene.forEach<Mesh>([&](EntityId, const Mesh& m) {
        if (m.mesh && seenMeshes.insert(m.mesh.id()).second) {
            const auto& asset = resources.get(m.mesh);
            emitDescriptor(meshes, asset.name, asset.source);
        }
        if (m.material && seenMaterials.insert(m.material.id()).second) {
            const auto& asset = resources.get(m.material);
            // Materials always save as `inline` — captures the actual runtime
            // state (including editor scalar tweaks) regardless of how the
            // material was originally created.
            emitDescriptor(materials, asset.name, materialToInline(asset, resources));
            // Pull every texture this material references into the texture
            // descriptor pool too.
            for (const auto& f : kMaterialTextureFields) emitTexture(asset.*f.member);
        }
    });

    nlohmann::json out;
    out["textures"]  = std::move(textures);
    out["meshes"]    = std::move(meshes);
    out["materials"] = std::move(materials);
    return out;
}

bool loadAssets(const nlohmann::json& assetsJson, ResourceManager& resources) {
    if (!assetsJson.is_object()) {
        LOG_WARNING("AssetSerializer::loadAssets: assets block is not an object — skipping");
        return false;
    }

    const auto& factories = AssetFactories::get();

    auto loadGroup = [&](const char* sectionName, auto&& createOne) {
        if (!assetsJson.contains(sectionName) || !assetsJson[sectionName].is_array()) return;
        for (const auto& entry : assetsJson[sectionName]) {
            const std::string name = entry.value("name", std::string{});
            if (name.empty()) continue;
            const nlohmann::json source = entry.contains("source") ? entry["source"] : nlohmann::json{};
            createOne(name, source);
        }
    };

    // Textures first — materials may reference them by name. Order is
    // important: textures → materials → meshes. Meshes don't depend on the
    // others; they're last only for output stability.
    loadGroup("textures", [&](const std::string& name, const nlohmann::json& source) {
        if (resources.findByName<TextureAsset>(name)) return;
        TextureHandle h = factories.createTexture(source, resources);
        if (!h) {
            LOG_WARNING("AssetSerializer: texture '%s' could not be recreated — skipping", name.c_str());
            return;
        }
        // Texture loaders set name + source themselves; we don't overwrite.
        // (If a custom loader didn't, the asset is still findable until next
        // session — log so we notice.)
        if (resources.get(h).name != name) {
            LOG_WARNING("AssetSerializer: texture loader did not set name '%s' (got '%s')",
                name.c_str(), resources.get(h).name.c_str());
        }
    });

    loadGroup("materials", [&](const std::string& name, const nlohmann::json& source) {
        if (resources.findByName<MaterialAsset>(name)) return;
        MaterialHandle h = factories.createMaterial(source, resources);
        if (!h) {
            LOG_WARNING("AssetSerializer: material '%s' could not be recreated — skipping", name.c_str());
            return;
        }
        resources.edit(h).name = name;
    });

    loadGroup("meshes", [&](const std::string& name, const nlohmann::json& source) {
        if (resources.findByName<MeshAsset>(name)) return;
        MeshAsset mesh = factories.createMesh(source);
        if (mesh.vertices.empty()) {
            LOG_WARNING("AssetSerializer: mesh '%s' recreated empty — skipping", name.c_str());
            return;
        }
        mesh.source = source;
        resources.add(std::move(mesh), name);
    });

    return true;
}

// Expose the inline applier for asset_registration.cpp to use when
// registering the inline material factory.
void applyInline(const nlohmann::json& src, MaterialAsset& m, const ResourceManager& resources) {
    applyInlineMaterial(src, m, resources);
}

} // namespace AssetSerializer

} // namespace Engine
