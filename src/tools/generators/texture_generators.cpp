#include "texture_generators.h"

#include <cstdint>

#include "logger.h"
#include "resource_manager.h"

namespace Engine {

TextureHandle generateSolidColorTexture(
    glm::vec4 color,
    ResourceManager& resourceManager,
    bool srgb
) {
    // Create 1x1 texture
    TextureAsset texture;
    texture.width = 1;
    texture.height = 1;
    texture.internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    texture.format = GL_RGBA;
    texture.type = GL_UNSIGNED_BYTE;
    texture.generateMipmaps = false;  // No mipmaps needed for 1x1
    texture.srgb = srgb;
    texture.filePath = "procedural:solid_color";

    // Convert color from [0,1] to [0,255]
    texture.pixelData.resize(4);
    texture.pixelData[0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
    texture.pixelData[1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
    texture.pixelData[2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
    texture.pixelData[3] = static_cast<uint8_t>(glm::clamp(color.a, 0.0f, 1.0f) * 255.0f);

    return resourceManager.add(std::move(texture));
}

TextureHandle generateWhiteTexture(ResourceManager& resourceManager) {
    return generateSolidColorTexture(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), resourceManager, false);
}

TextureHandle generateBlackTexture(ResourceManager& resourceManager) {
    return generateSolidColorTexture(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), resourceManager, false);
}

TextureHandle generateNormalTexture(ResourceManager& resourceManager) {
    // Normal map pointing straight up: (0, 0, 1) in world space
    // In texture space: (0.5, 0.5, 1.0) which maps to (128, 128, 255) in [0,255]
    TextureAsset texture;
    texture.width = 1;
    texture.height = 1;
    texture.internalFormat = GL_RGB8;
    texture.format = GL_RGB;
    texture.type = GL_UNSIGNED_BYTE;
    texture.generateMipmaps = false;
    texture.srgb = false;  // Normal maps are NOT sRGB
    texture.filePath = "procedural:default_normal";

    texture.pixelData.resize(3);
    texture.pixelData[0] = 128;  // X: 0 (neutral)
    texture.pixelData[1] = 128;  // Y: 0 (neutral)
    texture.pixelData[2] = 255;  // Z: 1 (pointing up)

    return resourceManager.add(std::move(texture));
}

TextureHandle generateGrayTexture(ResourceManager& resourceManager) {
    return generateSolidColorTexture(glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), resourceManager, false);
}

} // namespace Engine
