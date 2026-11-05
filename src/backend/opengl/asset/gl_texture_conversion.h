#pragma once

#include <algorithm>

#include <GL/glew.h>

#include "vkm_opengl/texture/texture.h"

#include "resource/asset/texture_params.h"

namespace Vkm::Engine {

// The engine's texture enums in GL terms. Each switch names every enumerator, so a new one warns
// under -Wswitch; the return after it is for the compiler, as AssetCook::readTexture refuses a byte
// that names no enumerator.

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
        case TextureInternalFormat::Count:    break;
    }
    return GL_RGBA8;
}

/**
 * @brief Whether a texture uploaded as @p glFormat stores color in sRGB.
 *
 * Answered through toGLenum and isSrgbFormat, so the engine's rule is the only list.
 *
 * @param glFormat The texture's GL internal format.
 * @return True when an engine sRGB format uploads as @p glFormat.
 */
inline bool isSrgbGLFormat(GLenum glFormat) {
    for (int f = 0; f < static_cast<int>(TextureInternalFormat::Count); ++f) {
        const auto format = static_cast<TextureInternalFormat>(f);
        if (toGLenum(format) == glFormat) return isSrgbFormat(format);
    }
    return false;
}

inline GLenum toGLenum(TexturePixelFormat fmt) {
    switch (fmt) {
        case TexturePixelFormat::R:     return GL_RED;
        case TexturePixelFormat::RG:    return GL_RG;
        case TexturePixelFormat::RGB:   return GL_RGB;
        case TexturePixelFormat::RGBA:  return GL_RGBA;
        case TexturePixelFormat::Count: break;
    }
    return GL_RGBA;
}

inline GLenum toGLenum(TexturePixelType type) {
    switch (type) {
        case TexturePixelType::UnsignedByte: return GL_UNSIGNED_BYTE;
        case TexturePixelType::Float:        return GL_FLOAT;
        case TexturePixelType::HalfFloat:    return GL_HALF_FLOAT;
        case TexturePixelType::Count:        break;
    }
    return GL_UNSIGNED_BYTE;
}

inline Vkm::OpenGL::TextureWrap toGLWrap(TextureWrapMode wrap) {
    switch (wrap) {
        case TextureWrapMode::Repeat:         return Vkm::OpenGL::TextureWrap::Repeat;
        case TextureWrapMode::MirroredRepeat: return Vkm::OpenGL::TextureWrap::MirroredRepeat;
        case TextureWrapMode::ClampToEdge:    return Vkm::OpenGL::TextureWrap::ClampToEdge;
        case TextureWrapMode::ClampToBorder:  return Vkm::OpenGL::TextureWrap::ClampToBorder;
        case TextureWrapMode::Count:          break;
    }
    return Vkm::OpenGL::TextureWrap::ClampToEdge;
}

/**
 * @brief The GL sampler state one texture is drawn with.
 */
struct GLTextureFilter {
    Vkm::OpenGL::TextureMinFilter min        = Vkm::OpenGL::TextureMinFilter::LinearMipmapLinear;
    Vkm::OpenGL::TextureMagFilter mag        = Vkm::OpenGL::TextureMagFilter::Linear;
    float                         anisotropy = 1.0f;
};

/**
 * @brief How one texture is sampled.
 *
 * The texture's override wins where it states one: Nearest on an asset is a claim about its
 * content (a lookup table blended is wrong at any quality), not a preference. A mipmap filter on a
 * texture with level 0 alone leaves it incomplete, which GL samples as black.
 *
 * @param assetOverride TextureParams::filterOverride.
 * @param mipmapped     Whether the texture carries a mip chain.
 * @param maxAnisotropy The GL layer clamps it to the driver's ceiling.
 * @return The min filter, mag filter and anisotropy to apply.
 */
inline GLTextureFilter resolveTextureFilter(
    TextureFilterOverride assetOverride,
    bool mipmapped,
    float maxAnisotropy
) {
    GLTextureFilter out;
    if (assetOverride == TextureFilterOverride::Nearest) {
        out.min = mipmapped
            ? Vkm::OpenGL::TextureMinFilter::NearestMipmapNearest
            : Vkm::OpenGL::TextureMinFilter::Nearest;
        out.mag = Vkm::OpenGL::TextureMagFilter::Nearest;
        return out;
    }
    out.min = mipmapped
        ? Vkm::OpenGL::TextureMinFilter::LinearMipmapLinear
        : Vkm::OpenGL::TextureMinFilter::Linear;
    out.anisotropy = maxAnisotropy;
    return out;
}

/**
 * @brief Convert engine TextureParams to Vkm::OpenGL::Texture2DParams for GPU upload.
 *
 * Describes level 0 alone. A texture carrying its levels, or blocks, is created empty and filled
 * per level (GLAssetTexture::update); GL builds a chain only where buildsMipsAtUpload says. The
 * filter is the asset's own: the frame's anisotropy is layered on after upload
 * (GLAssetMirror::setTextureAnisotropy).
 *
 * @param params The texture's description.
 * @param data   Level 0's texels, or null to allocate it unfilled.
 * @return The parameters Vkm::OpenGL::Texture2D is constructed from.
 */
inline Vkm::OpenGL::Texture2DParams toGLParams(const TextureParams& params, const void* data) {
    const GLTextureFilter filter = resolveTextureFilter(params.filterOverride, isMipmapped(params), 1.0f);

    Vkm::OpenGL::Texture2DParams gl;
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
    gl.levels          = gl.generateMipmaps ? 0 : std::max(params.mipLevels, 1u);
    gl.data            = data;
    return gl;
}

} // namespace Vkm::Engine
