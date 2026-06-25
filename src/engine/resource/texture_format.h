#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief Backend-agnostic texture descriptors. The GL backend maps these to its
 * concrete GLenum equivalents at upload time; other backends do likewise.
 */

/** @brief GPU-side storage format (channels + bit depth + color space). */
enum class TextureInternalFormat : uint8_t {
    R8,
    RG8,
    RGB8,
    RGBA8,
    SRGB8,
    SRGBA8,
    RGBA16F,
    RGBA32F
};

/** @brief Channel layout of the source pixel data passed on upload. */
enum class TexturePixelFormat : uint8_t {
    R,
    RG,
    RGB,
    RGBA
};

/** @brief Component type of the source pixel data passed on upload. */
enum class TexturePixelType : uint8_t {
    UnsignedByte,
    Float,
    HalfFloat
};

/** @brief How sampling behaves for UVs outside [0,1]. */
enum class TextureWrapMode : uint8_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder
};

/** @brief Minification filter (the Mipmap variants require generated mipmaps). */
enum class TextureMinFilter : uint8_t {
    Nearest,
    Linear,
    NearestMipmapNearest,
    LinearMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapLinear
};

/** @brief Magnification filter. */
enum class TextureMagFilter : uint8_t {
    Nearest,
    Linear
};

/**
 * @brief Full sampling + storage description for a texture, consumed by the
 * backend on GPU upload. Defaults give a mipmapped, edge-clamped RGBA8 map.
 */
struct TextureParams {
    uint32_t width  = 0;
    uint32_t height = 0;

    TextureInternalFormat internalFormat = TextureInternalFormat::RGBA8;
    TexturePixelFormat    format         = TexturePixelFormat::RGBA;
    TexturePixelType      type           = TexturePixelType::UnsignedByte;

    TextureWrapMode  wrapS     = TextureWrapMode::ClampToEdge;
    TextureWrapMode  wrapT     = TextureWrapMode::ClampToEdge;
    TextureMinFilter minFilter = TextureMinFilter::LinearMipmapLinear;
    TextureMagFilter magFilter = TextureMagFilter::Linear;

    bool generateMipmaps = true;
};

} // namespace Engine
