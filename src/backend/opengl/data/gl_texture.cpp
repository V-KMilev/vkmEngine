#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_texture.h"

#include <string>

#include "convention/gl_format_conversion.h"
#include "texture/gl_texture.h"

#include "resource/asset/texture_asset.h"

namespace Engine {

GLTexture::GLTexture(const TextureAsset& texture) {
    update(texture);
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

} // namespace Engine
