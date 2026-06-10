#pragma once

#include <GL/glew.h>

#include "resource/texture_format.h"
#include "texture/gl_texture.h"

namespace Engine {

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

inline Core::TextureWrap toGLWrap(TextureWrapMode wrap) {
    switch (wrap) {
        case TextureWrapMode::Repeat:         return Core::TextureWrap::Repeat;
        case TextureWrapMode::MirroredRepeat: return Core::TextureWrap::MirroredRepeat;
        case TextureWrapMode::ClampToEdge:    return Core::TextureWrap::ClampToEdge;
        case TextureWrapMode::ClampToBorder:  return Core::TextureWrap::ClampToBorder;
    }
    return Core::TextureWrap::ClampToEdge;
}

inline Core::TextureMinFilter toGLMinFilter(TextureMinFilter filter) {
    switch (filter) {
        case TextureMinFilter::Nearest:              return Core::TextureMinFilter::Nearest;
        case TextureMinFilter::Linear:               return Core::TextureMinFilter::Linear;
        case TextureMinFilter::NearestMipmapNearest: return Core::TextureMinFilter::NearestMipmapNearest;
        case TextureMinFilter::LinearMipmapNearest:  return Core::TextureMinFilter::LinearMipmapNearest;
        case TextureMinFilter::NearestMipmapLinear:  return Core::TextureMinFilter::NearestMipmapLinear;
        case TextureMinFilter::LinearMipmapLinear:   return Core::TextureMinFilter::LinearMipmapLinear;
    }
    return Core::TextureMinFilter::LinearMipmapLinear;
}

inline Core::TextureMagFilter toGLMagFilter(TextureMagFilter filter) {
    switch (filter) {
        case TextureMagFilter::Nearest: return Core::TextureMagFilter::Nearest;
        case TextureMagFilter::Linear:  return Core::TextureMagFilter::Linear;
    }
    return Core::TextureMagFilter::Linear;
}

/**
 * @brief Convert engine TextureParams to Core::Texture2DParams for GPU upload.
 */
inline Core::Texture2DParams toGLParams(const TextureParams& params, const void* data) {
    Core::Texture2DParams gl;
    gl.width          = params.width;
    gl.height         = params.height;
    gl.internalFormat = toGLenum(params.internalFormat);
    gl.format         = toGLenum(params.format);
    gl.type           = toGLenum(params.type);
    gl.wrapS          = toGLWrap(params.wrapS);
    gl.wrapT          = toGLWrap(params.wrapT);
    gl.minFilter      = toGLMinFilter(params.minFilter);
    gl.magFilter      = toGLMagFilter(params.magFilter);
    gl.generateMipmaps = params.generateMipmaps;
    gl.data           = data;
    return gl;
}

} // namespace Engine
