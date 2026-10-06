#pragma once

#include <GL/glew.h>

#include "resource/texture_format.h"
#include "system/render/render_settings.h"
#include "gl_texture.h"

namespace Vkm::Engine {

// The engine's texture enums in GL terms. Each switch names every enumerator, so a new one warns
// under -Wswitch; the return after it answers a value none holds, as a damaged cooked byte would.

inline GLenum toGLenum(TextureInternalFormat fmt) {
    switch (fmt) {
        case TextureInternalFormat::R8:       return GL_R8;
        case TextureInternalFormat::RG8:      return GL_RG8;
        case TextureInternalFormat::RGB8:     return GL_RGB8;
        case TextureInternalFormat::RGBA8:    return GL_RGBA8;
        case TextureInternalFormat::SRGB8:    return GL_SRGB8;
        case TextureInternalFormat::SRGBA8:   return GL_SRGB8_ALPHA8;
        case TextureInternalFormat::RGBA16F:  return GL_RGBA16F;
        case TextureInternalFormat::RGBA32F:  return GL_RGBA32F;
        case TextureInternalFormat::BC4R:     return GL_COMPRESSED_RED_RGTC1;
        case TextureInternalFormat::BC5RG:    return GL_COMPRESSED_RG_RGTC2;
        case TextureInternalFormat::BC7RGBA:  return GL_COMPRESSED_RGBA_BPTC_UNORM;
        case TextureInternalFormat::BC7SRGBA: return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
    }
    return GL_RGBA8;
}

/**
 * @brief Whether a texture uploaded as @p glFormat stores colour in sRGB.
 *
 * Answered through toGLenum and isSrgbFormat, so the engine's rule is the only list.
 *
 * @param glFormat The texture's GL internal format.
 * @return True when an engine sRGB format uploads as @p glFormat.
 */
inline bool isSrgbGLFormat(GLenum glFormat) {
    // BC7SRGBA is the enum's last value.
    for (int f = 0; f <= static_cast<int>(TextureInternalFormat::BC7SRGBA); ++f) {
        const auto format = static_cast<TextureInternalFormat>(f);
        if (toGLenum(format) == glFormat) return isSrgbFormat(format);
    }
    return false;
}

inline GLenum toGLenum(TexturePixelFormat fmt) {
    switch (fmt) {
        case TexturePixelFormat::R:    return GL_RED;
        case TexturePixelFormat::RG:   return GL_RG;
        case TexturePixelFormat::RGB:  return GL_RGB;
        case TexturePixelFormat::RGBA: return GL_RGBA;
    }
    return GL_RGBA;
}

inline GLenum toGLenum(TexturePixelType type) {
    switch (type) {
        case TexturePixelType::UnsignedByte: return GL_UNSIGNED_BYTE;
        case TexturePixelType::Float:        return GL_FLOAT;
        case TexturePixelType::HalfFloat:    return GL_HALF_FLOAT;
    }
    return GL_UNSIGNED_BYTE;
}

inline Vkm::GL::TextureWrap toGLWrap(TextureWrapMode wrap) {
    switch (wrap) {
        case TextureWrapMode::Repeat:         return Vkm::GL::TextureWrap::Repeat;
        case TextureWrapMode::MirroredRepeat: return Vkm::GL::TextureWrap::MirroredRepeat;
        case TextureWrapMode::ClampToEdge:    return Vkm::GL::TextureWrap::ClampToEdge;
        case TextureWrapMode::ClampToBorder:  return Vkm::GL::TextureWrap::ClampToBorder;
    }
    return Vkm::GL::TextureWrap::ClampToEdge;
}

/**
 * @brief The GL sampler state one texture is drawn with.
 */
struct GLTextureFilter {
    Vkm::GL::TextureMinFilter min        = Vkm::GL::TextureMinFilter::LinearMipmapLinear;
    Vkm::GL::TextureMagFilter mag        = Vkm::GL::TextureMagFilter::Linear;
    float                     anisotropy = 1.0f;
};

/**
 * @brief Resolve how one texture is sampled from the two things that get a say.
 *
 * The texture's override wins where it states one: Nearest on an asset is a claim about its
 * content (a lookup table blended is wrong at any quality), not a preference. Otherwise
 * @p sceneMode. A mipmap filter on a texture with level 0 alone leaves it incomplete, which GL
 * samples as black.
 *
 * @param assetOverride TextureParams::filterOverride.
 * @param mipmapped     Whether the texture carries a mip chain.
 * @param sceneMode     RenderSettings::textureFiltering for this frame.
 * @param maxAnisotropy Layered on Trilinear alone; the GL layer clamps it to the driver's ceiling.
 * @return The min filter, mag filter and anisotropy to apply.
 */
inline GLTextureFilter resolveTextureFilter(
    TextureFilterOverride assetOverride,
    bool mipmapped,
    TextureFiltering sceneMode,
    float maxAnisotropy
) {
    const TextureFiltering mode = (assetOverride == TextureFilterOverride::Nearest)
        ? TextureFiltering::Nearest
        : sceneMode;

    GLTextureFilter out;
    switch (mode) {
        case TextureFiltering::Nearest:
            out.min = mipmapped
                ? Vkm::GL::TextureMinFilter::NearestMipmapNearest
                : Vkm::GL::TextureMinFilter::Nearest;
            out.mag = Vkm::GL::TextureMagFilter::Nearest;
            break;
        case TextureFiltering::Bilinear:
            out.min = mipmapped
                ? Vkm::GL::TextureMinFilter::LinearMipmapNearest
                : Vkm::GL::TextureMinFilter::Linear;
            break;
        case TextureFiltering::Trilinear:
            out.min = mipmapped
                ? Vkm::GL::TextureMinFilter::LinearMipmapLinear
                : Vkm::GL::TextureMinFilter::Linear;
            out.anisotropy = maxAnisotropy;
            break;
        // Stored by nothing; named so a fourth filter warns under -Wswitch rather than sampling
        // with GLTextureFilter's defaults.
        case TextureFiltering::Count:
            break;
    }
    return out;
}

/**
 * @brief The filter a texture is created with, before the scene has its say.
 *
 * The frame's setting is layered on after upload (see GLView::setTextureFiltering).
 *
 * @param params The texture's own sampling description.
 * @return The asset's filter, with no scene setting layered on.
 */
inline GLTextureFilter uploadTextureFilter(const TextureParams& params) {
    // Trilinear with no anisotropy is how a setting says "no opinion".
    return resolveTextureFilter(
        params.filterOverride,
        isMipmapped(params),
        TextureFiltering::Trilinear,
        1.0f
    );
}

/**
 * @brief Convert engine TextureParams to Vkm::GL::Texture2DParams for GPU upload.
 *
 * Describes level 0 alone. A texture carrying its levels, or blocks, is created empty and filled
 * per level (GLTexture::update); GL builds a chain only where buildsMipsAtUpload says.
 *
 * @param params The texture's description.
 * @param data   Level 0's texels, or null to allocate it unfilled.
 * @return The parameters Vkm::GL::Texture2D is constructed from.
 */
inline Vkm::GL::Texture2DParams toGLParams(const TextureParams& params, const void* data) {
    const GLTextureFilter filter = uploadTextureFilter(params);

    Vkm::GL::Texture2DParams gl;
    gl.width           = params.width;
    gl.height          = params.height;
    gl.internalFormat  = toGLenum(params.internalFormat);
    gl.format          = toGLenum(params.format);
    gl.type            = toGLenum(params.type);
    gl.wrapS           = toGLWrap(params.wrapS);
    gl.wrapT           = toGLWrap(params.wrapT);
    gl.minFilter       = filter.min;
    gl.magFilter       = filter.mag;
    gl.maxAnisotropy   = filter.anisotropy;
    gl.generateMipmaps = buildsMipsAtUpload(params);
    gl.data            = data;
    return gl;
}

} // namespace Vkm::Engine
