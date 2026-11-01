#define VKM_LOG_CATEGORY "IO"

#include "io/asset/recipe/material_recipe.h"

#include <set>
#include <string>
#include <utility>

#include "vkm_log/logger.h"

#include "io/json/reflect_json.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

nlohmann::json materialToRecipe(const MaterialAsset& m, const ResourceManager& resources) {
    // The reflection driver writes every field, so a new MaterialAsset field cannot fall out of the
    // round trip. Texture refs resolve by name, below.
    nlohmann::json src = ReflectJson::saveFields(m, ReflectJson::PlainData{});

    nlohmann::json textures = nlohmann::json::object();
    for (const auto& f : MATERIAL_TEXTURE_FIELDS) {
        const TextureHandle& h = m.*f.member;
        if (!h) continue;
        const auto& tex = resources.get(h);
        // Warned here, not on every load: a hidden texture is not in the cooked manifest, so its name in a
        // public material's recipe can never resolve.
        if (tex.isHidden()) {
            LOG_WARNING(
                "Material texture slot '%s' refers to hidden asset '%s' - dropping ref",
                f.key,
                tex.name().c_str()
            );
            continue;
        }
        if (tex.name().empty()) {
            LOG_WARNING("Material texture slot '%s' has no name - dropping ref", f.key);
            continue;
        }
        textures[f.key] = tex.name();
    }
    if (!textures.empty()) src["textures"] = std::move(textures);
    return src;
}

MaterialAsset materialFromRecipe(
    const nlohmann::json& src,
    const std::string& name,
    const ResourceManager& resources
) {
    // Mirror of materialToRecipe. A missing key, or an enum name this build lacks, reads as the default
    // rather than as enumerator zero; a value of the wrong type throws, for the caller to report.
    MaterialAsset m;
    ReflectJson::loadFields(src, m, ReflectJson::PlainData{});

    // What materialToRecipe writes with every map named: any other key is one this build drops.
    static const nlohmann::json s_shape = [] {
        nlohmann::json shape = ReflectJson::saveFields(MaterialAsset{}, ReflectJson::PlainData{});
        for (const TexField& f : MATERIAL_TEXTURE_FIELDS) shape["textures"][f.key] = "";
        return shape;
    }();
    std::set<std::string> unread;
    ReflectJson::unreadKeys(src, s_shape, "", unread);
    for (const std::string& key : unread) {
        LOG_WARNING(
            "Material '%s': its recipe's '%s' is no field this build has (schema drift; dropped)",
            name.c_str(),
            key.c_str()
        );
    }

    if (src.contains("textures") && src["textures"].is_object()) {
        for (const auto& f : MATERIAL_TEXTURE_FIELDS) {
            if (!src["textures"].contains(f.key)) continue;
            const std::string texName = src["textures"][f.key].get<std::string>();
            if (texName.empty()) continue;
            const TextureHandle h = resources.findByName<TextureAsset>(texName);
            if (!h) {
                LOG_WARNING(
                    "Material texture ref '%s' ('%s') unresolved; the map is left empty",
                    f.key,
                    texName.c_str()
                );
                continue;
            }
            m.*f.member = h;
        }
    }
    return m;
}

} // namespace Vkm::Engine
