#pragma once

#include <GL/glew.h>

#include "gl_texture.h"

namespace Vkm::Engine {

/**
 * @brief A float lookup table's texture: read by its texel centres, bilinear, clamped, no levels.
 *
 * @param width          Texels across.
 * @param height         Texels down.
 * @param internalFormat Its storage, such as GL_RG16F.
 * @param format         The channels that storage holds, such as GL_RG.
 * @return The table's parameters, with no pixels.
 */
inline Vkm::GL::Texture2DParams lookupTableParams(
    int width,
    int height,
    GLenum internalFormat,
    GLenum format
) {
    Vkm::GL::Texture2DParams params;
    params.width           = width;
    params.height          = height;
    params.internalFormat  = internalFormat;
    params.format          = format;
    params.type            = GL_FLOAT;
    params.wrapS           = Vkm::GL::TextureWrap::ClampToEdge;
    params.wrapT           = Vkm::GL::TextureWrap::ClampToEdge;
    params.minFilter       = Vkm::GL::TextureMinFilter::Linear;
    params.magFilter       = Vkm::GL::TextureMagFilter::Linear;
    params.generateMipmaps = false;
    return params;
}

} // namespace Vkm::Engine
