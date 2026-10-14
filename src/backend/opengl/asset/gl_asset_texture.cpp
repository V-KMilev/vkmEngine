#include "asset/gl_asset_texture.h"

#include <memory>
#include <string>

#include <GL/glew.h>

#include "convention/gl_format_conversion.h"
#include "gl_texture.h"

#include "loader/image_loaders.h"
#include "resource/asset/texture_asset.h"
#include "resource/asset/font_asset.h"

namespace Vkm::Engine {

namespace {

// Whether pixels go up a level at a time: blocks must, and a chain is more than one level.
bool uploadsByLevel(const TextureParams& params) {
    return params.mipLevels > 1 || isCompressedFormat(params.internalFormat);
}

// Fill every level the asset carries into @p texture, created with them unfilled.
void uploadLevels(const Vkm::GL::Texture2D& texture, const TextureAsset& asset) {
    const TextureParams& params = asset.params;
    const bool           blocks = isCompressedFormat(params.internalFormat);

    const uint8_t* level = asset.pixelData.data();
    for (uint32_t index = 0; index < params.mipLevels; ++index) {
        const uint64_t bytes = textureLevelBytes(params, index);
        if (blocks) texture.uploadCompressedLevel(index, level, static_cast<uint32_t>(bytes));
        else        texture.uploadLevel(index, level);
        level += bytes;
    }
}

// Every channel of a one-channel map reads as its grey, so a shader taking roughness from green
// and metalness from blue gets what a grey colour file would give, not zero.
void swizzleGrey(Vkm::GL::Texture2D& texture, TextureInternalFormat format) {
    if (format != TextureInternalFormat::R8 && format != TextureInternalFormat::BC4R) return;
    const GLint grey[4] = {GL_RED, GL_RED, GL_RED, GL_ONE};
    texture.setSwizzle(grey);
}

// The last upload id handed out. Textures upload on the render thread alone.
uint64_t g_lastUpload = 0;

} // namespace

GLTexture::GLTexture(const TextureAsset& texture) {
    update(texture);
}

GLTexture::GLTexture(const FontAsset& font) {
    update(font);
}

GLTexture::~GLTexture() = default;

void GLTexture::update(const TextureAsset& texture) {
    const void* data    = texture.pixelData.empty() ? nullptr : texture.pixelData.data();
    const bool  byLevel = uploadsByLevel(texture.params);
    const Vkm::GL::Texture2DParams params = toGLParams(texture.params, byLevel ? nullptr : data);

    // Rebuilt, not refilled, whenever pixels arrive: one first made while loading has the stub's
    // format and wrap, so a refill would read an sRGB albedo as linear or clamp a tiling texture.
    if (!m_texture || data) {
        // Kept for applyFiltering, which runs after the asset is out of reach. Taken with the
        // texture: a stub asking for levels the GPU texture lacks would leave it incomplete.
        m_filterOverride = texture.params.filterOverride;
        m_mipmapped      = isMipmapped(texture.params);

        const std::string name = texture.name().empty()
            ? ("texture_" + std::to_string(texture.version()))
            : texture.name();
        m_texture = std::make_unique<Vkm::GL::Texture2D>(name, params);
        if (data && byLevel) uploadLevels(*m_texture, texture);
        swizzleGrey(*m_texture, texture.params.internalFormat);
        m_uploadId = ++g_lastUpload;
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
    params.minFilter       = Vkm::GL::TextureMinFilter::LinearMipmapLinear;
    params.magFilter       = Vkm::GL::TextureMagFilter::Linear;
    params.generateMipmaps = false;
    // Text is drawn far smaller than baked, so the atlas needs levels, but only as many as the
    // baker left a gutter for, or the smallest would blend neighbouring glyphs.
    params.levels          = FontAsset::MIP_LEVELS;
    params.data            = font.atlasPixels.empty() ? nullptr : font.atlasPixels.data();

    if (!m_texture) {
        m_texture = std::make_unique<Vkm::GL::Texture2D>(font.name() + ":atlas", params);
    } else if (params.data) {
        m_texture->setData(params.data, params.width, params.height, GL_RED, GL_UNSIGNED_BYTE);
    }
    m_uploadId = ++g_lastUpload;
    if (!params.data) return;

    m_texture->generateMipmaps();
    m_hasPixels = true;
}

void GLTexture::applyFiltering(TextureFiltering sceneMode, float maxAnisotropy) {
    const GLTextureFilter filter =
        resolveTextureFilter(m_filterOverride, m_mipmapped, sceneMode, maxAnisotropy);

    m_texture->setFilter(filter.min, filter.mag);
    m_texture->setMaxAnisotropy(filter.anisotropy);
}

std::unique_ptr<Vkm::GL::Texture2D> uploadImageFile(const std::string& path) {
    const DecodedImage image = decodeImageRGBA(path);
    if (!image.isValid()) return nullptr;

    Vkm::GL::Texture2DParams params;
    params.width           = image.width;
    params.height          = image.height;
    params.internalFormat  = GL_SRGB8_ALPHA8;
    params.generateMipmaps = true;
    params.minFilter       = Vkm::GL::TextureMinFilter::LinearMipmapLinear;
    params.data            = image.pixels.data();
    return std::make_unique<Vkm::GL::Texture2D>(path, params);
}

} // namespace Vkm::Engine
