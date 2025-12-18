#pragma once

#include "resource.h"
#include "resource_handle.h"

namespace Engine {

/**
 * @brief Enum of supported texture formats.
 */
 enum class TextureFormat : uint32_t {
    RGBA8,    ///< 8-bit unsigned per channel, 4 channels (standard color)
};

/**
 * @brief Enum of supported texture wrap modes.
 */
enum class TextureWrap : uint32_t {
    Repeat,            ///< Texture repeats (tiles)
    MirroredRepeat,    ///< Texture mirrors and repeats
    ClampToEdge        ///< Texture clamps to edge pixel color
};

/**
 * @brief Enum of supported texture minification filters.
 */
enum class TextureMinFilter : uint32_t {
    Nearest,                 ///< Nearest neighbor sampling (no interpolation)
    Linear,                  ///< Linear interpolation
    NearestMipmapNearest,    ///< Nearest mipmap, nearest texel
    LinearMipmapNearest,     ///< Nearest mipmap, linear texel
    NearestMipmapLinear,     ///< Linear mipmap, nearest texel
    LinearMipmapLinear       ///< Linear mipmap, linear texel (trilinear filtering)
};

/**
 * @brief Enum of supported texture magnification filters.
 */
enum class TextureMagFilter : uint32_t {
    Nearest,    ///< Nearest neighbor sampling (no interpolation)
    Linear      ///< Linear interpolation
};

struct TextureAsset : public Resource {
    uint32_t width  = 0;
    uint32_t height = 0;

    TextureFormat format       = TextureFormat::RGBA8;
    TextureWrap wrapS          = TextureWrap::ClampToEdge;
    TextureWrap wrapT          = TextureWrap::ClampToEdge;
    TextureMinFilter minFilter = TextureMinFilter::Linear;
    TextureMagFilter magFilter = TextureMagFilter::Linear;

    bool generateMipmaps = false;
    bool srgb            = false;

    std::vector<uint8_t> data = {};
};

using TextureHandle = Handle<TextureAsset>;

} // namespace Engine