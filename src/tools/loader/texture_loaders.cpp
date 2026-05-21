#include "texture_loaders.h"

#include <cstdint>
#include <cstring>

#include <nlohmann/json.hpp>

#include "logger.h"
#include "resource/resource_manager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Engine {

namespace {
    /**
     * @brief Helper to infer texture internal format from channel count and sRGB flag.
     */
    TextureInternalFormat inferInternalFormat(int channels, bool srgb) {
        if (srgb) {
            switch (channels) {
                case 3: return TextureInternalFormat::SRGB8;
                case 4: return TextureInternalFormat::SRGBA8;
                default: return TextureInternalFormat::SRGBA8;
            }
        }
        switch (channels) {
            case 1: return TextureInternalFormat::R8;
            case 2: return TextureInternalFormat::RG8;
            case 3: return TextureInternalFormat::RGB8;
            case 4: return TextureInternalFormat::RGBA8;
            default: return TextureInternalFormat::RGBA8;
        }
    }

    /**
     * @brief Helper to infer texture pixel format from channel count.
     */
    TexturePixelFormat inferFormat(int channels) {
        switch (channels) {
            case 1: return TexturePixelFormat::R;
            case 2: return TexturePixelFormat::RG;
            case 3: return TexturePixelFormat::RGB;
            case 4: return TexturePixelFormat::RGBA;
            default: return TexturePixelFormat::RGBA;
        }
    }
}

TextureHandle loadTexture(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb,
    bool generateMipmaps
) {
    // Load image using stb_image
    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &channels, 0);

    if (!data) {
        LOG_ERROR("Failed to load texture from '%s': %s", filePath.c_str(), stbi_failure_reason());
        return TextureHandle{}; // Return invalid handle
    }

    // Create TextureAsset
    TextureAsset texture;
    texture.params.width = static_cast<uint32_t>(width);
    texture.params.height = static_cast<uint32_t>(height);
    texture.params.internalFormat = inferInternalFormat(channels, srgb);
    texture.params.format = inferFormat(channels);
    texture.params.type = TexturePixelType::UnsignedByte;
    texture.params.generateMipmaps = generateMipmaps;
    texture.srgb = srgb;
    texture.filePath = filePath;

    // Copy pixel data
    const size_t dataSize = width * height * channels;
    texture.pixelData.resize(dataSize);
    std::memcpy(texture.pixelData.data(), data, dataSize);

    // Free stb_image data
    stbi_image_free(data);

    LOG_INFO("Loaded texture '%s' (%dx%d, %d channels, sRGB: %s)",
        filePath.c_str(), width, height, channels, srgb ? "yes" : "no");

    // Stamp serialization metadata. The file path is the texture's stable
    // identity — materials reference textures by name on load.
    texture.name         = filePath;
    texture.sourceJson() = {
        {"kind",           "file"},
        {"path",           filePath},
        {"sRGB",           srgb},
        {"generateMipmaps", generateMipmaps},
    };
    return resourceManager.add(std::move(texture));
}

} // namespace Engine
