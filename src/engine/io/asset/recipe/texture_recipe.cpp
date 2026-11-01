#include "io/asset/recipe/texture_recipe.h"

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "debug/error/engine_error_log.h"
#include "io/asset/recipe/asset_source.h"
#include "base/reflect/enum_names.h"

namespace Vkm::Engine {

namespace {

// A wrap mode's recipe spelling, or null past the last mode; ClampToEdge's is never written. The
// switch has no default label, so -Wswitch flags a mode added without one.
const char* wrapName(TextureWrapMode mode) {
    switch (mode) {
        case TextureWrapMode::Repeat:         return "repeat";
        case TextureWrapMode::MirroredRepeat: return "mirror";
        case TextureWrapMode::ClampToEdge:    return "clamp";
        case TextureWrapMode::ClampToBorder:  return "border";
        case TextureWrapMode::Count:          break;
    }
    return nullptr;
}

constexpr const char* FILTER_NEAREST = "nearest";

// A recipe key's string, or empty when it holds something else - reported as unknown, not thrown.
std::string recipeString(const nlohmann::json& source, const char* key) {
    const auto found = source.find(key);
    return found != source.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

} // namespace

TextureUsage textureUsageFromRecipe(const nlohmann::json& source) {
    TextureUsage usage = TextureUsage::Data;
    const auto named   = source.find(AssetSourceKey::USAGE);
    if (named == source.end()) return usage;
    if (named->is_string() && Reflect::enumFromNameChecked(named->get<std::string>(), usage)) return usage;

    std::string known;
    for (const char* name : Reflect::EnumNames<TextureUsage>::values) {
        known += known.empty() ? name : std::string(", ") + name;
    }
    reportUnknownTextureValue(source, AssetSourceKey::USAGE, known, Reflect::enumName(TextureUsage::Data));
    return TextureUsage::Data;
}

void reportUnknownTextureValue(
    const nlohmann::json& source,
    const char* key,
    const std::string& known,
    const char* readAs
) {
    const auto path = source.find(AssetSourceKey::PATH);
    const std::string recipe = path != source.end() && path->is_string()
        ? "texture recipe for '" + path->get<std::string>() + "'"
        : std::string("a texture recipe");
    reportError(
        "Asset",
        recipe,
        std::string("names ") + key + " " + source[key].dump() + ", which is not one of " + known
            + "; read as " + readAs
    );
}

nlohmann::json fileTextureRecipe(
    const std::string& ref,
    TextureUsage usage,
    bool generateMipmaps,
    TextureFilterOverride filterOverride,
    TextureWrapMode wrap
) {
    nlohmann::json source = {
        {AssetSourceKey::KIND,             AssetSourceKind::FILE},
        {AssetSourceKey::PATH,             ref},
        {AssetSourceKey::USAGE,            Reflect::enumName(usage)},
        {AssetSourceKey::GENERATE_MIPMAPS, generateMipmaps},
    };
    if (filterOverride == TextureFilterOverride::Nearest) source[AssetSourceKey::FILTER] = FILTER_NEAREST;
    if (wrap != TextureWrapMode::ClampToEdge) source[AssetSourceKey::WRAP] = wrapName(wrap);
    return source;
}

TextureFilterOverride textureFilterFromRecipe(const nlohmann::json& source) {
    if (!source.contains(AssetSourceKey::FILTER)) return TextureFilterOverride::None;
    const std::string filter = recipeString(source, AssetSourceKey::FILTER);
    if (filter == FILTER_NEAREST) return TextureFilterOverride::Nearest;
    reportUnknownTextureValue(source, AssetSourceKey::FILTER, FILTER_NEAREST, "none");
    return TextureFilterOverride::None;
}

bool textureMipmapsFromRecipe(const nlohmann::json& source) {
    return source.value(AssetSourceKey::GENERATE_MIPMAPS, true);
}

TextureWrapMode textureWrapFromRecipe(const nlohmann::json& source) {
    if (!source.contains(AssetSourceKey::WRAP)) return TextureWrapMode::ClampToEdge;
    const std::string wrap = recipeString(source, AssetSourceKey::WRAP);
    std::string known;
    for (uint8_t value = 0; const char* name = wrapName(static_cast<TextureWrapMode>(value)); ++value) {
        if (wrap == name) return static_cast<TextureWrapMode>(value);
        known += known.empty() ? name : std::string(", ") + name;
    }
    reportUnknownTextureValue(source, AssetSourceKey::WRAP, known, wrapName(TextureWrapMode::ClampToEdge));
    return TextureWrapMode::ClampToEdge;
}

} // namespace Vkm::Engine
