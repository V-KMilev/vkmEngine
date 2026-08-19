#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief Backend-agnostic texture descriptors. The GL backend maps these to its
 * concrete GLenum equivalents at upload time; other backends do likewise.
 *
 * Every enum below is persisted by value in cooked textures, so its enumerator
 * order is part of that file format: append only. Reordering one silently
 * re-reads every already-cooked texture as a different format, which is why
 * writeTexture in io/asset/asset_cook.cpp pins every enumerator of each.
 */

/**
 * @brief GPU-side storage format (channels + bit depth + color space).
 */
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

/**
 * @brief Channel layout of the source pixel data passed on upload.
 */
enum class TexturePixelFormat : uint8_t {
    R,
    RG,
    RGB,
    RGBA
};

/**
 * @brief Component type of the source pixel data passed on upload.
 */
enum class TexturePixelType : uint8_t {
    UnsignedByte,
    Float,
    HalfFloat
};

/**
 * @brief How sampling behaves for UVs outside [0,1].
 */
enum class TextureWrapMode : uint8_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder
};

/**
 * @brief Minification filter (the Mipmap variants require generated mipmaps).
 */
enum class TextureMinFilter : uint8_t {
    Nearest,
    Linear,
    NearestMipmapNearest,
    LinearMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapLinear
};

/**
 * @brief Magnification filter.
 */
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

/**
 * @brief Infer the GPU storage format from channel count and sRGB flag.
 *
 * The stb decode path only knows the channel count, so format selection is
 * inferred here. Shared by the synchronous loader and the async finaliser so
 * both agree on the mapping. Unexpected counts fall back to (S)RGBA8.
 */
inline TextureInternalFormat inferInternalFormat(int channels, bool srgb) {
    if (srgb) {
        return (channels == 3) ? TextureInternalFormat::SRGB8
                               : TextureInternalFormat::SRGBA8;
    }
    switch (channels) {
        case 1:  return TextureInternalFormat::R8;
        case 2:  return TextureInternalFormat::RG8;
        case 3:  return TextureInternalFormat::RGB8;
        case 4:
        default: return TextureInternalFormat::RGBA8;
    }
}

/**
 * @brief Infer the source pixel layout from channel count (defaults to RGBA).
 */
inline TexturePixelFormat inferFormat(int channels) {
    switch (channels) {
        case 1:  return TexturePixelFormat::R;
        case 2:  return TexturePixelFormat::RG;
        case 3:  return TexturePixelFormat::RGB;
        case 4:
        default: return TexturePixelFormat::RGBA;
    }
}

} // namespace Engine
