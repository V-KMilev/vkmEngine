#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_texture.h"

#include <string>

#include "convention/gl_format_conversion.h"
#include "texture/gl_texture.h"

#include "resource/asset/texture_asset.h"
#include "resource/asset/font_asset.h"

namespace Vkm::Engine {

GLTexture::GLTexture(const TextureAsset& texture) {
    update(texture);
}

GLTexture::GLTexture(const FontAsset& font) {
    update(font);
}

GLTexture::~GLTexture() = default;

void GLTexture::update(const TextureAsset& texture) {
    const void* data = texture.pixelData.empty() ? nullptr : texture.pixelData.data();
    const Vkm::GL::Texture2DParams params = toGLParams(texture.params, data);

    // Kept because applyFiltering runs every frame, long after the asset itself
    // is out of reach here, and needs both halves to resolve.
    m_filterOverride = texture.params.filterOverride;
    m_mipmapped      = texture.params.generateMipmaps;

    if (!m_texture) {
        const std::string name = texture.filePath.empty()
            ? ("texture_" + std::to_string(texture.version))
            : texture.filePath;
        m_texture = std::make_unique<Vkm::GL::Texture2D>(name, params);
    } else if (data) {
        m_texture->setData(data, texture.params.width, texture.params.height,
                           params.format, params.type);
    }
    if (data) m_hasPixels = true;
}

void GLTexture::update(const FontAsset& font) {
    Vkm::GL::Texture2DParams params;
    params.width           = font.atlasSize;
    params.height          = font.atlasSize;
    params.internalFormat  = GL_R8;
    params.format          = GL_RED;
    params.type            = GL_UNSIGNED_BYTE;
    params.minFilter       = Vkm::GL::TextureMinFilter::Linear;
    params.magFilter       = Vkm::GL::TextureMagFilter::Linear;
    params.generateMipmaps = false;
    params.data            = font.atlasPixels.empty() ? nullptr : font.atlasPixels.data();

    if (!m_texture) {
        m_texture = std::make_unique<Vkm::GL::Texture2D>(font.name + ":atlas", params);
    } else if (params.data) {
        m_texture->setData(params.data, params.width, params.height, GL_RED, GL_UNSIGNED_BYTE);
    }
    if (params.data) m_hasPixels = true;
}

void GLTexture::applyFiltering(TextureFiltering sceneMode, float maxAnisotropy) {
    const GLTextureFilter filter =
        resolveTextureFilter(m_filterOverride, m_mipmapped, sceneMode, maxAnisotropy);

    m_texture->setFilter(filter.min, filter.mag);
    m_texture->setMaxAnisotropy(filter.anisotropy);
}

} // namespace Vkm::Engine
