#include "resource/gl_texture.h"

#include "logger.h"

#include "config/gl_format_conversion.h"
#include "gl_texture.h"  // Core::Texture2D
#include "resource/texture_asset.h"

namespace Engine {

GLTexture::GLTexture(const TextureAsset& texture) {
    update(texture);
}

GLTexture::~GLTexture() {
    m_texture.reset();
    LOG_TRACE("Destructed GLTexture");
}

void GLTexture::update(const TextureAsset& texture) {
    const void* data = texture.pixelData.empty() ? nullptr : texture.pixelData.data();
    const Core::Texture2DParams glParams = toGLParams(texture.params, data);

    if (!m_texture) {
        std::string name = texture.filePath.empty()
            ? ("Texture_" + std::to_string(texture.version))
            : texture.filePath;
        m_texture = std::make_unique<Core::Texture2D>(name, glParams);
    } else {
        if (!texture.pixelData.empty()) {
            m_texture->setData(
                texture.pixelData.data(),
                texture.params.width,
                texture.params.height,
                glParams.format,
                glParams.type
            );
        }
        m_texture->setWrap(glParams.wrapS, glParams.wrapT);
        m_texture->setFilter(glParams.minFilter, glParams.magFilter);
    }
}

void GLTexture::bind(uint32_t slot) const {
    if (m_texture) {
        m_texture->bindSlot(slot);
    }
}

} // namespace Engine
