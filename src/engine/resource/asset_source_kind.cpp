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

} // namespace Vkm::Engine
