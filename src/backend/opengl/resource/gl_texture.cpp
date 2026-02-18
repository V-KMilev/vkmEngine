#include "gl_texture.h"

#include "logger.h"

#include "resource/texture_asset.h"

#include "gl_texture.h"  // Core::Texture2D

namespace Engine {

GLTexture::GLTexture(const TextureAsset& texture) {
    update(texture);
}

GLTexture::~GLTexture() {
    m_texture.reset();
    LOG_TRACE("Destroying GLTexture");
}

void GLTexture::update(const TextureAsset& texture) {
    // Get Texture2DParams with data pointer properly synchronized
    const Core::Texture2DParams& params = texture.getParams();

    // Create or update the texture
    if (!m_texture) {
        // Generate a name from the texture handle or use a default
        std::string name = texture.filePath.empty() 
            ? ("Texture_" + std::to_string(texture.version))
            : texture.filePath;
        m_texture = std::make_unique<Core::Texture2D>(name, params);
    } else {
        // Update existing texture data
        if (!texture.pixelData.empty()) {
            m_texture->setData(
                texture.pixelData.data(),
                texture.width,
                texture.height,
                params.format,
                params.type
            );
        }
        // Update texture parameters
        m_texture->setWrap(params.wrapS, params.wrapT);
        m_texture->setFilter(params.minFilter, params.magFilter);
    }
}

void GLTexture::bind(uint32_t slot) const {
    if (m_texture) {
        m_texture->bindSlot(slot);
    }
}

} // namespace Engine
