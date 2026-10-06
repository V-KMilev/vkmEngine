#include "resource/asset_source_kind.h"

#include <string>

#include <nlohmann/json.hpp>

#include "debug/engine_error_log.h"

namespace Vkm::Engine {

TextureUsage textureUsageFromRecipe(const nlohmann::json& source) {
    TextureUsage usage = TextureUsage::Data;
    const auto named = source.find(AssetSourceKey::USAGE);
    if (named == source.end()) return usage;
    if (named->is_string() && Reflect::enumFromNameChecked(named->get<std::string>(), usage)) return usage;

    // A misspelt usage would show only as a pale colour map, so name the recipe.
    std::string known;
    for (const char* name : Reflect::EnumNames<TextureUsage>::values) {
        known += known.empty() ? name : std::string(", ") + name;
    }
    const auto path = source.find(AssetSourceKey::PATH);
    const std::string recipe = path != source.end() && path->is_string()
        ? "texture recipe for '" + path->get<std::string>() + "'"
        : std::string("a texture recipe");
    reportError(
        "Asset",
        recipe,
        "names usage " + named->dump() + ", which is not one of " + known + "; decoded as Data"
    );
    return TextureUsage::Data;
}

} // namespace Vkm::Engine
