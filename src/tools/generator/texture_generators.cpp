#include "texture_generators.h"

#include <cstdint>
#include <cstdio>

#include <glm/common.hpp>
#include <nlohmann/json.hpp>

#include "logger.h"
#include "resource/resource_manager.h"

namespace Engine {

namespace {
/// Built-in textures are reused via findByName - they're stable, immutable,
/// and naturally shared across materials. The same "builtin:white" handle
/// is returned every time the generator is asked for one.
TextureHandle getOrCreateNamed(ResourceManager& rm, const char* name,
                               const nlohmann::json& source,
                               TextureAsset texture)
{
    if (auto existing = rm.findByName<TextureAsset>(name)) return existing;
    texture.name         = name;
    texture.sourceJson() = source;
    return rm.add(std::move(texture));
}

TextureAsset makeSolidColorAsset(glm::vec4 color, bool srgb) {
    TextureAsset texture;
    texture.params.width = 1;
    texture.params.height = 1;
    texture.params.internalFormat = srgb ? TextureInternalFormat::SRGBA8 : TextureInternalFormat::RGBA8;
    texture.params.format = TexturePixelFormat::RGBA;
    texture.params.type = TexturePixelType::UnsignedByte;
    texture.params.generateMipmaps = false;
    texture.srgb = srgb;
    texture.filePath = "procedural:solid_color";

    texture.pixelData.resize(4);
    texture.pixelData[0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
    texture.pixelData[1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
    texture.pixelData[2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
    texture.pixelData[3] = static_cast<uint8_t>(glm::clamp(color.a, 0.0f, 1.0f) * 255.0f);
    return texture;
}

TextureAsset makeDefaultNormalAsset() {
    TextureAsset texture;
    texture.params.width = 1;
    texture.params.height = 1;
    texture.params.internalFormat = TextureInternalFormat::RGB8;
    texture.params.format = TexturePixelFormat::RGB;
    texture.params.type = TexturePixelType::UnsignedByte;
    texture.params.generateMipmaps = false;
    texture.srgb = false;
    texture.filePath = "procedural:default_normal";

    texture.pixelData.resize(3);
    texture.pixelData[0] = 128;
    texture.pixelData[1] = 128;
    texture.pixelData[2] = 255;
    return texture;
}
}

TextureHandle generateWhiteTexture(ResourceManager& rm) {
    return getOrCreateNamed(rm, "builtin:white",
        {{"kind", "builtin"}, {"type", "white"}},
        makeSolidColorAsset(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), false));
}

TextureHandle generateBlackTexture(ResourceManager& rm) {
    return getOrCreateNamed(rm, "builtin:black",
        {{"kind", "builtin"}, {"type", "black"}},
        makeSolidColorAsset(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), false));
}

TextureHandle generateNormalTexture(ResourceManager& rm) {
    return getOrCreateNamed(rm, "builtin:normal",
        {{"kind", "builtin"}, {"type", "normal"}},
        makeDefaultNormalAsset());
}

TextureHandle generateGrayTexture(ResourceManager& rm) {
    return getOrCreateNamed(rm, "builtin:gray",
        {{"kind", "builtin"}, {"type", "gray"}},
        makeSolidColorAsset(glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), false));
}

TextureHandle createSolidColorTexture(glm::vec4 color, ResourceManager& rm, bool srgb) {
    auto u8 = [](float c) { return static_cast<int>(glm::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
    // Deterministic id/name keyed on the quantised color + colorspace, so two
    // requests for the same solid dedup to one asset across the session/runs.
    char key[64];
    std::snprintf(key, sizeof(key), "texture:solid:%02X%02X%02X%02X:%d",
                  u8(color.r), u8(color.g), u8(color.b), u8(color.a), srgb ? 1 : 0);

    if (auto existing = rm.findByName<TextureAsset>(key)) return existing;

    TextureAsset tex = makeSolidColorAsset(color, srgb);
    tex.name         = key;  // stable, unique per color - the findByName dedup key.
    nlohmann::json src;
    src["kind"]  = "solid";
    src["color"] = {color.r, color.g, color.b, color.a};
    src["srgb"]  = srgb;
    tex.sourceJson() = std::move(src);
    return rm.add(std::move(tex));
}

} // namespace Engine
