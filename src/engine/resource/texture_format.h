#pragma once

#include <algorithm>
#include <cstdint>

#include "core/reflect.h"

namespace Vkm::Engine {

// Every enum TextureParams holds is persisted by value in cooked textures: append only. A reorder
// silently re-reads cooked textures as another format; writeTexture in io/asset/asset_cook.cpp pins
// every enumerator.

/**
 * @brief GPU-side storage format (channels + bit depth + color space).
 *
 * The BC formats store 4x4 texel blocks, and only the cooker produces them (cook/texture_bake.h).
 */
enum class TextureInternalFormat : uint8_t {
    R8,
    RG8,
    RGB8,
    RGBA8,
    SRGB8,
    SRGBA8,
    RGBA16F,
    RGBA32F,
    BC4R,       ///< One linear channel, 8 bytes a block (RGTC1).
    BC5RG,      ///< Two linear channels, 16 bytes a block (RGTC2).
    BC7RGBA,    ///< Linear RGBA, 16 bytes a block (BPTC).
    BC7SRGBA    ///< sRGB colour with linear alpha, 16 bytes a block (BPTC).
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
 * @brief A texture's override of RenderSettings::textureFiltering.
 *
 * Nearest is for content that is wrong when blended at any quality - pixel art, lookup tables, UI
 * sprites. Bilinear against trilinear is a cost question, left to the setting alone.
 */
enum class TextureFilterOverride : uint8_t {
    None,
    Nearest
};

/**
 * @brief Full sampling + storage description for a texture, consumed by the backend on upload.
 */
struct TextureParams {
    uint32_t width  = 0;
    uint32_t height = 0;

    TextureInternalFormat internalFormat = TextureInternalFormat::RGBA8;
    TexturePixelFormat    format         = TexturePixelFormat::RGBA;
    TexturePixelType      type           = TexturePixelType::UnsignedByte;

    TextureWrapMode wrapS = TextureWrapMode::ClampToEdge;
    TextureWrapMode wrapT = TextureWrapMode::ClampToEdge;

    TextureFilterOverride filterOverride = TextureFilterOverride::None;

    bool generateMipmaps = true;

    /// Levels the pixel data carries, level 0 first: one, or the whole chain down to 1x1.
    uint32_t mipLevels = 1;
};

/**
 * @brief What a texture's texels mean, which decides how they are decoded, stored, mipped and
 *        compressed.
 *
 * No image file says it, so the importer states it and the recipe records it under `usage`. Once
 * decoded, the storage format carries it (textureUsageOf).
 */
enum class TextureUsage : uint8_t {
    Color,   ///< sRGB albedo, emission: filtered as light, weighted by alpha.
    Data,    ///< Linear - roughness, a mask, a packed map: each channel apart.
    Normal,  ///< Tangent-space: x and y stored, z rebuilt where sampled.
    Count
};

/**
 * @brief How many channels an image file holding @p fileChannels is stored as.
 *
 * Grey+alpha widens to four (RG storage would sample no alpha), as does grey colour (no one-channel
 * sRGB format); grey data stays one, read as grey in every channel. A normal keeps x and y only:
 * z is rebuilt, and a two-channel block format holds better than a three-channel one.
 *
 * @param fileChannels Channels the file holds, as the decoder reports them.
 * @param usage        What the texels mean.
 * @return The count stored; inferInternalFormat and inferFormat map it consistently.
 */
inline int decodeChannels(int fileChannels, TextureUsage usage) {
    if (usage == TextureUsage::Normal) return 2;
    if (fileChannels == 2 || (fileChannels == 1 && usage == TextureUsage::Color)) return 4;
    return fileChannels;
}

/**
 * @brief Infer the GPU storage format from a stored channel count and a usage.
 *
 * Unexpected counts fall back to (S)RGBA8. Each usage gets formats no other takes - colour the
 * sRGB ones, a normal RG8, data the rest - so textureUsageOf can read it back.
 *
 * @param channels Channels stored per texel, as decodeChannels settled them.
 * @param usage    What the texels mean.
 * @return The storage format.
 */
inline TextureInternalFormat inferInternalFormat(int channels, TextureUsage usage) {
    if (usage == TextureUsage::Color) {
        return (channels == 3) ? TextureInternalFormat::SRGB8 : TextureInternalFormat::SRGBA8;
    }
    if (usage == TextureUsage::Normal) return TextureInternalFormat::RG8;
    switch (channels) {
        case 1:  return TextureInternalFormat::R8;
        case 3:  return TextureInternalFormat::RGB8;
        default: return TextureInternalFormat::RGBA8;
    }
}

/**
 * @brief Whether @p format stores colour in sRGB.
 *
 * @param format The GPU storage format.
 * @return True for the three sRGB formats.
 */
inline bool isSrgbFormat(TextureInternalFormat format) {
    return format == TextureInternalFormat::SRGB8 || format == TextureInternalFormat::SRGBA8
        || format == TextureInternalFormat::BC7SRGBA;
}

/**
 * @brief What a texture stored in @p format means; the inverse of inferInternalFormat, BC included.
 *
 * @param format The GPU storage format.
 * @return Color for the sRGB formats, Normal for RG8 and BC5, Data otherwise.
 */
inline TextureUsage textureUsageOf(TextureInternalFormat format) {
    if (isSrgbFormat(format)) return TextureUsage::Color;
    if (format == TextureInternalFormat::RG8 || format == TextureInternalFormat::BC5RG) {
        return TextureUsage::Normal;
    }
    return TextureUsage::Data;
}

/**
 * @brief Bytes one 4x4 block of @p format occupies, or 0 for a format stored by the texel.
 *
 * @param format The GPU storage format.
 * @return 8 for BC4, 16 for BC5 and BC7, 0 for every uncompressed format.
 */
inline uint64_t blockBytes(TextureInternalFormat format) {
    switch (format) {
        case TextureInternalFormat::BC4R:     return 8;
        case TextureInternalFormat::BC5RG:
        case TextureInternalFormat::BC7RGBA:
        case TextureInternalFormat::BC7SRGBA: return 16;
        case TextureInternalFormat::R8:
        case TextureInternalFormat::RG8:
        case TextureInternalFormat::RGB8:
        case TextureInternalFormat::RGBA8:
        case TextureInternalFormat::SRGB8:
        case TextureInternalFormat::SRGBA8:
        case TextureInternalFormat::RGBA16F:
        case TextureInternalFormat::RGBA32F:  return 0;
    }
    return 0;
}

/**
 * @brief Whether @p format stores 4x4 blocks rather than texels.
 *
 * @param format The GPU storage format.
 * @return True for the BC formats.
 */
inline bool isCompressedFormat(TextureInternalFormat format) {
    return blockBytes(format) != 0;
}

/**
 * @brief How many channels @p format lays out per texel.
 *
 * An out-of-range value off disk reads as RGBA, matching the backend (gl_format_conversion.h).
 *
 * @param format Channel layout of the pixel data.
 * @return 1 to 4.
 */
inline uint32_t channelCount(TexturePixelFormat format) {
    switch (format) {
        case TexturePixelFormat::R:    return 1;
        case TexturePixelFormat::RG:   return 2;
        case TexturePixelFormat::RGB:  return 3;
        case TexturePixelFormat::RGBA: return 4;
    }
    return 4;
}

/**
 * @brief Bytes one texel of uncompressed pixel data occupies.
 *
 * Out-of-range values read as RGBA / unsigned byte, matching the backend (gl_format_conversion.h).
 *
 * @param format Channel layout of the pixel data.
 * @param type   Component type of the pixel data.
 * @return Channels times component bytes.
 */
inline uint64_t bytesPerTexel(TexturePixelFormat format, TexturePixelType type) {
    const uint64_t channels = channelCount(format);
    uint64_t componentBytes = 1;
    switch (type) {
        case TexturePixelType::UnsignedByte: componentBytes = 1; break;
        case TexturePixelType::HalfFloat:    componentBytes = 2; break;
        case TexturePixelType::Float:        componentBytes = 4; break;
    }
    return channels * componentBytes;
}

/**
 * @brief How many levels a full mip chain of a @p width x @p height image has.
 *
 * @param width  Level-0 width.
 * @param height Level-0 height.
 * @return The level count including level 0; 0 for an empty image.
 */
inline uint32_t mipChainLength(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return 0;
    uint32_t extent = std::max(width, height);
    uint32_t levels = 1;
    while (extent > 1) {
        extent >>= 1;
        ++levels;
    }
    return levels;
}

/**
 * @brief One side of mip level @p level of an image whose level 0 is @p extent.
 *
 * @param extent Level-0 width or height.
 * @param level  Mip level, 0 being the image itself.
 * @return The side at that level, never below 1.
 */
inline uint32_t mipExtent(uint32_t extent, uint32_t level) {
    return (level >= 32) ? 1u : std::max(extent >> level, 1u);
}

/**
 * @brief Bytes of one storage unit of @p params' data: a block when compressed, else a texel.
 *
 * @param params The texture's description.
 * @return blockBytes for a BC format, bytesPerTexel otherwise.
 */
inline uint64_t textureUnitBytes(const TextureParams& params) {
    return isCompressedFormat(params.internalFormat)
        ? blockBytes(params.internalFormat)
        : bytesPerTexel(params.format, params.type);
}

/**
 * @brief How many storage units mip level @p level of @p params holds.
 *
 * A block-compressed side rounds up to a multiple of 4.
 *
 * @param params The texture's description.
 * @param level  Mip level, 0 being the image itself.
 * @return Blocks for a BC format, texels otherwise. Cannot wrap for any 32-bit sides.
 */
inline uint64_t textureLevelUnits(const TextureParams& params, uint32_t level) {
    const uint64_t width  = mipExtent(params.width, level);
    const uint64_t height = mipExtent(params.height, level);
    if (!isCompressedFormat(params.internalFormat)) return width * height;
    return ((width + 3) / 4) * ((height + 3) / 4);
}

/**
 * @brief The bytes mip level @p level of @p params occupies.
 *
 * Unbounded multiply: for trusted sides only; an untrusted reader divides first
 * (AssetCook::readTexture).
 *
 * @param params The texture's description.
 * @param level  Mip level, 0 being the image itself.
 * @return The level's byte count.
 */
inline uint64_t textureLevelBytes(const TextureParams& params, uint32_t level) {
    return textureLevelUnits(params, level) * textureUnitBytes(params);
}

/**
 * @brief Whether the backend builds the mip chain from level 0 when it uploads.
 *
 * Never for a block format, which GL cannot generate levels of.
 *
 * @param params The texture's description.
 * @return True when generateMipmaps is set and level 0 is uncompressed and alone.
 */
inline bool buildsMipsAtUpload(const TextureParams& params) {
    return params.generateMipmaps && params.mipLevels == 1 && !isCompressedFormat(params.internalFormat);
}

/**
 * @brief Whether the texture is sampled through a mip chain, carried or built at upload.
 *
 * Decides the minification filter: a mipmap filter over level 0 alone samples black in GL.
 *
 * @param params The texture's description.
 * @return True when there will be levels below 0.
 */
inline bool isMipmapped(const TextureParams& params) {
    return params.mipLevels > 1 || buildsMipsAtUpload(params);
}

/**
 * @brief Infer the source pixel layout from a stored channel count.
 *
 * @param channels Channels stored per texel.
 * @return The upload layout; RGBA for a count outside 1-4.
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

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::TextureUsage, "Color", "Data", "Normal")
