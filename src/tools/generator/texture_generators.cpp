#include "texture_generators.h"

#include <cstdint>

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
        texture.name           = name;
        texture.sourceJson()   = source;
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

TextureHandle generateSolidColorTexture(
    glm::vec4 color,
    ResourceManager& resourceManager,
    bool srgb
) {
    // Anonymous solid-color textures aren't deduped or stamped with a
    // builtin source - they're only suitable for transient runtime use.
    return resourceManager.add(makeSolidColorAsset(color, srgb));
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

} // namespace Engine
