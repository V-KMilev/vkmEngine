#include "texture_loaders.h"

#include <cstdint>

#include "logger.h"
#include "resource_manager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Engine {

namespace {
    /**
     * @brief Helper to infer OpenGL internal format from channel count and sRGB flag.
     */
    GLenum inferInternalFormat(int channels, bool srgb) {
        if (srgb) {
            switch (channels) {
                case 3: return GL_SRGB8;
                case 4: return GL_SRGB8_ALPHA8;
                default: return GL_SRGB8_ALPHA8;
            }
        }
        switch (channels) {
            case 1: return GL_R8;
            case 2: return GL_RG8;
            case 3: return GL_RGB8;
            case 4: return GL_RGBA8;
            default: return GL_RGBA8;
        }
    }

    /**
     * @brief Helper to infer OpenGL format from channel count.
     */
    GLenum inferFormat(int channels) {
        switch (channels) {
            case 1: return GL_RED;
            case 2: return GL_RG;
            case 3: return GL_RGB;
            case 4: return GL_RGBA;
            default: return GL_RGBA;
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
    texture.width = static_cast<uint32_t>(width);
    texture.height = static_cast<uint32_t>(height);
    texture.internalFormat = inferInternalFormat(channels, srgb);
    texture.format = inferFormat(channels);
    texture.type = GL_UNSIGNED_BYTE;
    texture.generateMipmaps = generateMipmaps;
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

    return resourceManager.add(std::move(texture));
}

} // namespace Engine
