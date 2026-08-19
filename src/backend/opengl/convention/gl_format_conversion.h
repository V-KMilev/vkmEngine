#pragma once

#include <GL/glew.h>

#include "resource/texture_format.h"
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

inline Vkm::GL::TextureMinFilter toGLMinFilter(TextureMinFilter filter) {
    switch (filter) {
        case TextureMinFilter::Nearest:              return Vkm::GL::TextureMinFilter::Nearest;
        case TextureMinFilter::Linear:               return Vkm::GL::TextureMinFilter::Linear;
        case TextureMinFilter::NearestMipmapNearest: return Vkm::GL::TextureMinFilter::NearestMipmapNearest;
        case TextureMinFilter::LinearMipmapNearest:  return Vkm::GL::TextureMinFilter::LinearMipmapNearest;
        case TextureMinFilter::NearestMipmapLinear:  return Vkm::GL::TextureMinFilter::NearestMipmapLinear;
        case TextureMinFilter::LinearMipmapLinear:   return Vkm::GL::TextureMinFilter::LinearMipmapLinear;
    }
    return Vkm::GL::TextureMinFilter::LinearMipmapLinear;
}

inline Vkm::GL::TextureMagFilter toGLMagFilter(TextureMagFilter filter) {
    switch (filter) {
        case TextureMagFilter::Nearest: return Vkm::GL::TextureMagFilter::Nearest;
        case TextureMagFilter::Linear:  return Vkm::GL::TextureMagFilter::Linear;
    }
    return Vkm::GL::TextureMagFilter::Linear;
}

/**
 * @brief Convert engine TextureParams to Vkm::GL::Texture2DParams for GPU upload.
 */
inline Vkm::GL::Texture2DParams toGLParams(const TextureParams& params, const void* data) {
    Vkm::GL::Texture2DParams gl;
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

} // namespace Vkm::Engine
