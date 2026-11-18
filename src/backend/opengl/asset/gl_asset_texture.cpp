#include "asset/gl_asset_texture.h"

#include <algorithm>
#include <memory>
#include <string>

#include <GL/glew.h>

#include "vkm_opengl/texture/texture.h"

#include "asset/gl_texture_conversion.h"

#include "decoded_image.h"
#include "resource/asset/texture_asset.h"
#include "resource/asset/font_asset.h"

namespace Vkm::Engine {

namespace {

constexpr float MAX_DATA_ANISOTROPY = 4.0f;  ///< The most anisotropy a map of normals or data takes: a
                                             ///< slant's specular shows less of it than its color does, and
                                             ///< each extra tap is paid at every pixel on a slant.

// Whether pixels go up a level at a time: blocks must, and a chain is more than one level.
bool uploadsByLevel(const TextureParams& params) {
    return params.mipLevels > 1 || isCompressedFormat(params.internalFormat);
}

// Fill every level the asset carries into @p texture, created with them unfilled.
void uploadLevels(const Vkm::OpenGL::Texture2D& texture, const TextureAsset& asset) {
    const TextureParams& params = asset.params;
    const bool blocks = isCompressedFormat(params.internalFormat);

    const uint8_t* level = asset.pixelData.data();
    for (uint32_t index = 0; index < params.mipLevels; ++index) {
        const uint64_t bytes = textureLevelBytes(params, index);
        if (blocks) texture.uploadCompressedLevel(index, level, static_cast<uint32_t>(bytes));
        else        texture.uploadLevel(index, level);
        level += bytes;
    }
}

// Every channel of a one-channel map reads as its gray, so a shader taking roughness from green
// and metalness from blue gets what a gray color file would give, not zero.
void swizzleGray(Vkm::OpenGL::Texture2D& texture, TextureInternalFormat format) {
    if (format != TextureInternalFormat::R8 && format != TextureInternalFormat::BC4R) return;
    const GLint gray[4] = {GL_RED, GL_RED, GL_RED, GL_ONE};
    texture.setSwizzle(gray);
}

// The last upload id handed out. Textures upload on the render thread alone.
uint64_t g_lastUpload = 0;

} // namespace

GLAssetTexture::GLAssetTexture(const TextureAsset& texture) {
    update(texture);
}

GLAssetTexture::GLAssetTexture(const FontAsset& font) {
    update(font);
}

GLAssetTexture::~GLAssetTexture() = default;

void GLAssetTexture::update(const TextureAsset& texture) {
    const void* data = texture.pixelData.empty() ? nullptr : texture.pixelData.data();
    const bool byLevel = uploadsByLevel(texture.params);
    const Vkm::OpenGL::Texture2DParams params = toGLParams(texture.params, byLevel ? nullptr : data);

    // Rebuilt, not refilled, whenever pixels arrive: one first made while loading has the stub's
    // format and wrap, so a refill would read an sRGB albedo as linear or clamp a tiling texture.
    if (!m_texture || data) {
        // Kept for applyFiltering, which runs after the asset is out of reach. Taken with the
        // texture: a stub asking for levels the GPU texture lacks would leave it incomplete.
        m_filterOverride = texture.params.filterOverride;
        m_mipmapped      = isMipmapped(texture.params);
        m_color          = texture.usage() == TextureUsage::Color;

        const std::string name = texture.name().empty()
            ? ("texture_" + std::to_string(texture.version()))
            : texture.name();
        m_texture = std::make_unique<Vkm::OpenGL::Texture2D>(name, params);
        if (data && byLevel) uploadLevels(*m_texture, texture);
        swizzleGray(*m_texture, texture.params.internalFormat);
        m_uploadId = ++g_lastUpload;
    }
    if (data) m_hasPixels = true;
}

void GLAssetTexture::update(const FontAsset& font) {
    Vkm::OpenGL::Texture2DParams params;
    params.width           = font.atlasSize;
    params.height          = font.atlasSize;
    params.internalFormat  = GL_R8;
    params.format          = GL_RED;
    params.type            = GL_UNSIGNED_BYTE;
    params.minFilter       = Vkm::OpenGL::TextureMinFilter::LinearMipmapLinear;
    params.magFilter       = Vkm::OpenGL::TextureMagFilter::Linear;
    params.generateMipmaps = false;
    // Text is drawn far smaller than baked, so the atlas needs levels, but only as many as the
    // baker left a gutter for, or the smallest would blend neighboring glyphs.
    params.levels          = FontAsset::MIP_LEVELS;
    params.data            = font.atlasPixels.empty() ? nullptr : font.atlasPixels.data();

    if (!m_texture) {
        m_texture = std::make_unique<Vkm::OpenGL::Texture2D>(font.name() + ":atlas", params);
    } else if (params.data) {
        m_texture->setData(params.data, params.width, params.height, GL_RED, GL_UNSIGNED_BYTE);
    }
    m_uploadId = ++g_lastUpload;
    if (!params.data) return;

    m_texture->generateMipmaps();
    m_hasPixels = true;
}

void GLAssetTexture::applyFiltering(float maxAnisotropy) {
    const float wanted = m_color ? maxAnisotropy : std::min(maxAnisotropy, MAX_DATA_ANISOTROPY);
    const GLTextureFilter filter = resolveTextureFilter(m_filterOverride, m_mipmapped, wanted);

    m_texture->setFilter(filter.min, filter.mag);
    m_texture->setMaxAnisotropy(filter.anisotropy);
}

} // namespace Vkm::Engine
