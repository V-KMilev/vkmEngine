#define VKM_LOG_CATEGORY "COOK"

#include "cook/texture_bake.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include <basisu_bc7e_scalar.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <stb_image_resize2.h>

#include "logger.h"

#include "cook/bc4_encoder.h"
#include "platform/threading/thread_pool.h"
#include "resource/texture_format.h"

namespace Vkm::Engine::AssetCooker {

namespace {

// MaterialAsset::alphaCutoff's default: a texture is cooked once and knows no material.
constexpr float COVERAGE_CUTOFF = 0.5f;

// The most a level's alpha may be scaled up, and enough bisection steps to pass 8-bit precision.
constexpr float MAX_COVERAGE_SCALE    = 64.0f;
constexpr int   COVERAGE_SEARCH_STEPS = 20;

// The block format a stored format compresses to, keeping channels and colour space.
TextureInternalFormat blockFormatOf(TextureInternalFormat format) {
    switch (format) {
        case TextureInternalFormat::R8:       return TextureInternalFormat::BC4R;
        case TextureInternalFormat::RG8:      return TextureInternalFormat::BC5RG;
        case TextureInternalFormat::SRGB8:
        case TextureInternalFormat::SRGBA8:   return TextureInternalFormat::BC7SRGBA;
        case TextureInternalFormat::RGB8:
        case TextureInternalFormat::RGBA8:
        case TextureInternalFormat::RGBA16F:
        case TextureInternalFormat::RGBA32F:
        case TextureInternalFormat::BC4R:
        case TextureInternalFormat::BC5RG:
        case TextureInternalFormat::BC7RGBA:
        case TextureInternalFormat::BC7SRGBA: break;
    }
    return TextureInternalFormat::BC7RGBA;
}

// The layout a compressed level decodes to, which gives the params' channel count.
TexturePixelFormat decodedLayoutOf(TextureInternalFormat blockFormat) {
    switch (blockFormat) {
        case TextureInternalFormat::BC4R:  return TexturePixelFormat::R;
        case TextureInternalFormat::BC5RG: return TexturePixelFormat::RG;
        default:                           return TexturePixelFormat::RGBA;
    }
}

// sRGB colour is alpha-weighted so transparent colour does not bleed; a linear four-channel
// texture is a packed mask, its channels filtered apart.
stbir_pixel_layout filterLayoutOf(uint32_t channels, bool srgb) {
    switch (channels) {
        case 1:  return STBIR_1CHANNEL;
        case 2:  return STBIR_2CHANNEL;
        case 3:  return STBIR_RGB;
        default: return srgb ? STBIR_RGBA : STBIR_4CHANNEL;
    }
}

stbir_edge edgeOf(TextureWrapMode wrap) {
    switch (wrap) {
        case TextureWrapMode::Repeat:         return STBIR_EDGE_WRAP;
        case TextureWrapMode::MirroredRepeat: return STBIR_EDGE_REFLECT;
        case TextureWrapMode::ClampToEdge:    return STBIR_EDGE_CLAMP;
        case TextureWrapMode::ClampToBorder:  return STBIR_EDGE_ZERO;
    }
    return STBIR_EDGE_CLAMP;
}

// bc7e fills read-only tables once, and no encode may run while it does.
void initEncoders() {
    static std::once_flag s_once;
    std::call_once(s_once, [] { bc7e_scalar::bc7e_compress_block_init(); });
}

using Bc7Params = bc7e_scalar::bc7e_compress_block_params;

Bc7Params bc7Params(bool srgb) {
    Bc7Params params;
    // Perceptual weights suit colour, not a normal or mask. The level is measured in resources.md.
    bc7e_scalar::bc7e_compress_block_params_init_basic(&params, srgb);
    return params;
}

// Compress one level, a row of blocks per task. Padding past the edge repeats the last column or
// row, so it cannot pull the endpoints toward a colour the level does not have.
void encodeLevel(
    const uint8_t* texels,
    uint32_t width,
    uint32_t height,
    uint32_t channels,
    TextureInternalFormat blockFormat,
    const Bc7Params& bc7,
    uint8_t* out
) {
    const uint32_t blocksWide = (width + 3) / 4;
    const uint32_t blocksHigh = (height + 3) / 4;
    const uint64_t bytes      = blockBytes(blockFormat);

    parallelFor(blocksHigh, 1, [&](size_t blockY) {
        // 16 RGBA texels per block, the shape bc7e encodes a run of blocks from.
        std::vector<uint32_t> row(static_cast<size_t>(blocksWide) * 16);
        for (uint32_t blockX = 0; blockX < blocksWide; ++blockX) {
            for (uint32_t y = 0; y < 4; ++y) {
                const uint32_t line = std::min(static_cast<uint32_t>(blockY) * 4 + y, height - 1);
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint32_t column = std::min(blockX * 4 + x, width - 1);
                    const uint8_t* texel  = texels + (static_cast<size_t>(line) * width + column) * channels;
                    uint8_t* pixel = reinterpret_cast<uint8_t*>(&row[blockX * 16 + y * 4 + x]);
                    pixel[0] = texel[0];
                    pixel[1] = channels > 1 ? texel[1] : 0;
                    pixel[2] = channels > 2 ? texel[2] : 0;
                    pixel[3] = channels > 3 ? texel[3] : 255;
                }
            }
        }

        uint8_t* rowOut = out + static_cast<size_t>(blockY) * blocksWide * bytes;
        if (blockFormat == TextureInternalFormat::BC4R || blockFormat == TextureInternalFormat::BC5RG) {
            const uint32_t used = blockFormat == TextureInternalFormat::BC4R ? 1 : 2;
            uint8_t channelsOf[16 * 2];
            for (uint32_t blockX = 0; blockX < blocksWide; ++blockX) {
                const uint8_t* rgba = reinterpret_cast<const uint8_t*>(&row[blockX * 16]);
                for (uint32_t i = 0; i < 16; ++i) {
                    for (uint32_t c = 0; c < used; ++c) channelsOf[i * used + c] = rgba[i * 4 + c];
                }
                uint8_t* block = rowOut + blockX * bytes;
                if (used == 1) encodeBC4Block(channelsOf, block);
                else           encodeBC5Block(channelsOf, block);
            }
        } else {
            bc7e_scalar::bc7e_compress_blocks(
                blocksWide,
                reinterpret_cast<uint64_t*>(rowOut),
                row.data(),
                &bc7
            );
        }
    });
}

// What a level is filtered as.
enum class Filtering {
    Colour,  ///< sRGB colour, decoded to light first; its alpha weights it.
    Data,    ///< Numbers, a channel at a time.
    Normal   ///< A direction: x and y unpacked, z rebuilt, renormalised after.
};

// The share of texels whose alpha, scaled by @p scale, clears the cutoff.
float coverageOf(const std::vector<float>& rgba, float scale) {
    const size_t texels = rgba.size() / 4;
    size_t covered = 0;
    for (size_t i = 0; i < texels; ++i) {
        if (rgba[i * 4 + 3] * scale > COVERAGE_CUTOFF) ++covered;
    }
    return texels ? static_cast<float>(covered) / static_cast<float>(texels) : 0.0f;
}

// The alpha scale under which @p rgba covers @p target, by bisection (coverage grows with scale).
// Keeps a filtered cut-out as dense at range as up close (Castano).
float coverageScale(const std::vector<float>& rgba, float target) {
    // Already there: left alone, not scaled down to the least that still is.
    if (coverageOf(rgba, 1.0f) == target) return 1.0f;
    float low  = 0.0f;
    float high = 1.0f;
    while (coverageOf(rgba, high) < target && high < MAX_COVERAGE_SCALE) high *= 2.0f;
    for (int step = 0; step < COVERAGE_SEARCH_STEPS; ++step) {
        const float middle = 0.5f * (low + high);
        if (coverageOf(rgba, middle) < target) low = middle;
        else                                   high = middle;
    }
    return high;
}

float srgbToLinear(uint8_t value) {
    static const std::array<float, 256> TABLE = [] {
        std::array<float, 256> table{};
        for (size_t i = 0; i < table.size(); ++i) {
            const float c = static_cast<float>(i) / 255.0f;
            table[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return table;
    }();
    return TABLE[value];
}

uint8_t quantise(float value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

uint8_t linearToSrgb(float value) {
    const float c = std::clamp(value, 0.0f, 1.0f);
    return quantise(c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
}

// Level 0 in the filtering space. A normal gains z: averaging directions needs all three.
std::vector<float> toLinear(const std::vector<uint8_t>& texels, uint32_t channels, Filtering filtering) {
    if (filtering == Filtering::Normal) {
        const size_t count = texels.size() / 2;
        std::vector<float> xyz(count * 3);
        for (size_t i = 0; i < count; ++i) {
            const float x = texels[i * 2 + 0] / 255.0f * 2.0f - 1.0f;
            const float y = texels[i * 2 + 1] / 255.0f * 2.0f - 1.0f;
            xyz[i * 3 + 0] = x;
            xyz[i * 3 + 1] = y;
            xyz[i * 3 + 2] = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
        }
        return xyz;
    }

    std::vector<float> out(texels.size());
    for (size_t i = 0; i < texels.size(); ++i) {
        // Alpha is a plain number in every format, sRGB ones included.
        const bool colour = filtering == Filtering::Colour && (channels < 4 || i % 4 != 3);
        out[i] = colour ? srgbToLinear(texels[i]) : texels[i] / 255.0f;
    }
    return out;
}

// A filtered normal is short where directions disagreed; unnormalised, its x and y rebuild a
// tilting z.
void renormalise(std::vector<float>& xyz) {
    for (size_t i = 0; i + 2 < xyz.size(); i += 3) {
        const glm::vec3 n(xyz[i], xyz[i + 1], xyz[i + 2]);
        const float length = glm::length(n);
        const glm::vec3 unit = length > glm::epsilon<float>() ? n / length : glm::vec3(0.0f, 0.0f, 1.0f);
        xyz[i + 0] = unit.x;
        xyz[i + 1] = unit.y;
        xyz[i + 2] = unit.z;
    }
}

// A filtered level back to stored bytes: the one place a level is rounded. A four-channel
// level's alpha is multiplied by @p alphaScale.
std::vector<uint8_t> toStored(
    const std::vector<float>& linear,
    uint32_t channels,
    Filtering filtering,
    float alphaScale
) {
    if (filtering == Filtering::Normal) {
        const size_t count = linear.size() / 3;
        std::vector<uint8_t> xy(count * 2);
        for (size_t i = 0; i < count; ++i) {
            xy[i * 2 + 0] = quantise(linear[i * 3 + 0] * 0.5f + 0.5f);
            xy[i * 2 + 1] = quantise(linear[i * 3 + 1] * 0.5f + 0.5f);
        }
        return xy;
    }

    std::vector<uint8_t> out(linear.size());
    for (size_t i = 0; i < linear.size(); ++i) {
        const bool alpha = channels == 4 && i % 4 == 3;
        if (alpha)                               out[i] = quantise(linear[i] * alphaScale);
        else if (filtering == Filtering::Colour) out[i] = linearToSrgb(linear[i]);
        else                                     out[i] = quantise(linear[i]);
    }
    return out;
}

} // namespace

bool bakeTexture(const TextureAsset& source, TextureAsset& out) {
    const TextureParams& params = source.params;

    // Already in its cooked form, or not something this knows how to filter.
    if (params.type != TexturePixelType::UnsignedByte || params.mipLevels != 1
        || isCompressedFormat(params.internalFormat)) {
        out.params    = params;
        out.pixelData = source.pixelData;
        return true;
    }

    const uint32_t channels = channelCount(params.format);
    const uint64_t expected = static_cast<uint64_t>(params.width) * params.height * channels;
    if (params.width == 0 || params.height == 0 || source.pixelData.size() != expected) {
        LOG_ERROR(
            "Texture '%s': %zu pixel byte(s) are not %ux%u texels of %u channel(s)",
            source.name().c_str(),
            source.pixelData.size(),
            params.width,
            params.height,
            channels
        );
        return false;
    }

    const bool      srgb      = isSrgbFormat(params.internalFormat);
    const Filtering filtering = source.usage() == TextureUsage::Normal && channels == 2
        ? Filtering::Normal
        : srgb ? Filtering::Colour : Filtering::Data;
    const uint32_t  levels    = params.generateMipmaps ? mipChainLength(params.width, params.height) : 1;
    // A normal is filtered as x, y and z, and stored as x and y.
    const uint32_t  filtered  = filtering == Filtering::Normal ? 3 : channels;
    // Each level keeps level 0's share of texels above the alpha cutoff.
    const bool      coverage  = filtering == Filtering::Colour && channels == 4;

    // Level 0 is the decoded source; each level below is filtered from the one above.
    std::vector<std::vector<uint8_t>> chain(levels);
    chain[0] = source.pixelData;

    std::vector<float> above = levels > 1
        ? toLinear(source.pixelData, channels, filtering)
        : std::vector<float>{};
    const float targetCoverage = coverage && levels > 1 ? coverageOf(above, 1.0f) : 0.0f;
    for (uint32_t level = 1; level < levels; ++level) {
        const int width      = static_cast<int>(mipExtent(params.width, level));
        const int height     = static_cast<int>(mipExtent(params.height, level));
        const int fromWidth  = static_cast<int>(mipExtent(params.width, level - 1));
        const int fromHeight = static_cast<int>(mipExtent(params.height, level - 1));
        std::vector<float> below(static_cast<size_t>(width) * height * filtered);
        const bool resized = stbir_resize(
            above.data(),
            fromWidth,
            fromHeight,
            0,
            below.data(),
            width,
            height,
            0,
            filterLayoutOf(filtered, srgb),
            STBIR_TYPE_FLOAT,
            edgeOf(params.wrapS),
            STBIR_FILTER_DEFAULT
        );
        if (!resized) {
            LOG_ERROR("Texture '%s': mip level %u could not be filtered", source.name().c_str(), level);
            return false;
        }
        if (filtering == Filtering::Normal) renormalise(below);
        const float alphaScale = coverage ? coverageScale(below, targetCoverage) : 1.0f;
        chain[level] = toStored(below, channels, filtering, alphaScale);
        above = std::move(below);
    }

    out.params           = params;
    out.params.mipLevels = levels;

    const bool compress = params.width >= 4 && params.height >= 4
        && params.filterOverride != TextureFilterOverride::Nearest;
    if (compress) {
        out.params.internalFormat = blockFormatOf(params.internalFormat);
        out.params.format         = decodedLayoutOf(out.params.internalFormat);
    }

    uint64_t total = 0;
    for (uint32_t level = 0; level < levels; ++level) total += textureLevelBytes(out.params, level);
    out.pixelData.resize(static_cast<size_t>(total));

    if (compress) initEncoders();
    const Bc7Params bc7 = bc7Params(srgb);

    uint8_t* write = out.pixelData.data();
    for (uint32_t level = 0; level < levels; ++level) {
        const uint64_t bytes = textureLevelBytes(out.params, level);
        if (compress) {
            encodeLevel(
                chain[level].data(),
                mipExtent(params.width, level),
                mipExtent(params.height, level),
                channels,
                out.params.internalFormat,
                bc7,
                write
            );
        } else {
            std::memcpy(write, chain[level].data(), static_cast<size_t>(bytes));
        }
        write += bytes;
    }
    return true;
}

} // namespace Vkm::Engine::AssetCooker
