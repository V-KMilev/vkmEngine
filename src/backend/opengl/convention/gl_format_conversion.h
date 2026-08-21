#pragma once

#include <GL/glew.h>

#include "resource/texture_format.h"
#include "system/render/render_settings.h"
#include "texture/gl_texture.h"

namespace Vkm::Engine {

// Map the engine's backend-agnostic texture enums to the GL / Vkm::GL::Texture2D
// equivalents for upload. Each falls back to a sane default on an unhandled
// value rather than asserting, so a new enum case degrades instead of crashing.

inline GLenum toGLenum(TextureInternalFormat fmt) {
    switch (fmt) {
        case TextureInternalFormat::R8:      return GL_R8;
        case TextureInternalFormat::RG8:     return GL_RG8;
        case TextureInternalFormat::RGB8:    return GL_RGB8;
        case TextureInternalFormat::RGBA8:   return GL_RGBA8;
        case TextureInternalFormat::SRGB8:   return GL_SRGB8;
        case TextureInternalFormat::SRGBA8:  return GL_SRGB8_ALPHA8;
        case TextureInternalFormat::RGBA16F: return GL_RGBA16F;
        case TextureInternalFormat::RGBA32F: return GL_RGBA32F;
    }
    return GL_RGBA8;
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
 * The texture's own override wins wherever it states one, because Nearest on an
 * asset is a claim about its content rather than a quality preference: a lookup
 * table blended with its neighbour is as wrong at 16x as at 1x. A texture that
 * states no override follows @p sceneMode, which is every texture the filtering
 * setting can meaningfully ask a question of - so the setting still reaches the
 * whole scene, and the two can never answer for the same texture at once.
 *
 * Whether the texture has a mip chain constrains both. A mipmap minification
 * filter over a texture that carries level 0 alone leaves it incomplete, and GL
 * samples an incomplete texture as opaque black rather than reporting it.
 *
 * @param assetOverride The texture's own say, TextureParams::filterOverride.
 * @param mipmapped     Whether the texture carries a mip chain.
 * @param sceneMode     RenderSettings::textureFiltering for this frame.
 * @param maxAnisotropy Requested degree; layered on Trilinear alone, and
 *                      clamped to the driver's ceiling by the GL layer.
 * @return The min filter, mag filter and anisotropy degree to apply.
 */
inline GLTextureFilter resolveTextureFilter(TextureFilterOverride assetOverride,
                                            bool mipmapped,
                                            TextureFiltering sceneMode,
                                            float maxAnisotropy) {
    const TextureFiltering mode = (assetOverride == TextureFilterOverride::Nearest)
        ? TextureFiltering::Nearest
        : sceneMode;

    GLTextureFilter out;
    switch (mode) {
        case TextureFiltering::Nearest:
            out.min = mipmapped ? Vkm::GL::TextureMinFilter::NearestMipmapNearest
                                : Vkm::GL::TextureMinFilter::Nearest;
            out.mag = Vkm::GL::TextureMagFilter::Nearest;
            break;
        case TextureFiltering::Bilinear:
            out.min = mipmapped ? Vkm::GL::TextureMinFilter::LinearMipmapNearest
                                : Vkm::GL::TextureMinFilter::Linear;
            break;
        case TextureFiltering::Trilinear:
            out.min = mipmapped ? Vkm::GL::TextureMinFilter::LinearMipmapLinear
                                : Vkm::GL::TextureMinFilter::Linear;
            out.anisotropy = maxAnisotropy;
            break;
    }
    return out;
}

/**
 * @brief The filter a texture is created with, before the scene has its say.
 *
 * Upload happens inside GLView::sync, and GLView::setTextureFiltering pushes
 * the frame's setting over the whole table immediately afterwards - so this
 * needs to be the texture's own half of the answer and nothing more, complete
 * enough to stand on its own if it were ever left alone.
 *
 * @param params The texture's own sampling description.
 * @return The asset's filter, with no scene setting layered on.
 */
inline GLTextureFilter uploadTextureFilter(const TextureParams& params) {
    // Trilinear is the setting-shaped spelling of "no opinion": ordinary
    // mipmapped sampling, with no anisotropy asked for on top of it.
    return resolveTextureFilter(params.filterOverride, params.generateMipmaps,
                                TextureFiltering::Trilinear, 1.0f);
}

/**
 * @brief Convert engine TextureParams to Vkm::GL::Texture2DParams for GPU upload.
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
    gl.generateMipmaps = params.generateMipmaps;
    gl.data            = data;
    return gl;
}

} // namespace Vkm::Engine
