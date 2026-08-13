#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_texture.h"

#include <string>

#include "convention/gl_format_conversion.h"
#include "texture/gl_texture.h"

#include "resource/asset/texture_asset.h"
#include "resource/asset/font_asset.h"

namespace Engine {

GLTexture::GLTexture(const TextureAsset& texture) {
    update(texture);
}

GLTexture::GLTexture(const FontAsset& font) {
    update(font);
}

GLTexture::~GLTexture() = default;

void GLTexture::update(const TextureAsset& texture) {
    const void* data = texture.pixelData.empty() ? nullptr : texture.pixelData.data();
    const Core::Texture2DParams params = toGLParams(texture.params, data);

    if (!m_texture) {
        const std::string name = texture.filePath.empty()
            ? ("texture_" + std::to_string(texture.version))
            : texture.filePath;
        m_texture = std::make_unique<Core::Texture2D>(name, params);
    } else if (data) {
        m_texture->setData(data, texture.params.width, texture.params.height,
                           params.format, params.type);
    }
}

void GLTexture::update(const FontAsset& font) {
    Core::Texture2DParams params;
    params.width           = font.atlasSize;
    params.height          = font.atlasSize;
    params.internalFormat  = GL_R8;
    params.format          = GL_RED;
    params.type            = GL_UNSIGNED_BYTE;
    params.minFilter       = Core::TextureMinFilter::Linear;
    params.magFilter       = Core::TextureMagFilter::Linear;
    params.generateMipmaps = false;
    params.data            = font.atlasPixels.empty() ? nullptr : font.atlasPixels.data();

    if (!m_texture) {
        m_texture = std::make_unique<Core::Texture2D>(font.name + ":atlas", params);
    } else if (params.data) {
        m_texture->setData(params.data, params.width, params.height, GL_RED, GL_UNSIGNED_BYTE);
    }
}

} // namespace Engine
