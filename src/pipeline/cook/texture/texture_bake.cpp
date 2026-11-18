#define VKM_LOG_CATEGORY "COOK"

#include "cook/texture/texture_bake.h"

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

#include "vkm_log/logger.h"

#include "cook/texture/bc4_encoder.h"
#include "portable_math.h"
#include "vkm_platform/threading/thread_pool.h"
#include "resource/asset/texture_params.h"

namespace Vkm::Engine::AssetCooker {

namespace {

// MaterialAsset::alphaCutoff's default: a texture is cooked once and knows no material.
constexpr float COVERAGE_CUTOFF = 0.5f;

// The most a level's alpha may be scaled up, and enough bisection steps to pass 8-bit precision.
constexpr float MAX_COVERAGE_SCALE    = 64.0f;
constexpr int   COVERAGE_SEARCH_STEPS = 20;

// The block format a stored format compresses to, keeping channels and color space.
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
        case TextureInternalFormat::BC7SRGBA:
        case TextureInternalFormat::Count:    break;
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

// sRGB color is alpha-weighted so transparent color does not bleed; a linear four-channel
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
        case TextureWrapMode::Count:          break;
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
    // Perceptual weights suit color, not a normal or mask.
    bc7e_scalar::bc7e_compress_block_params_init_basic(&params, srgb);
    return params;
}

// Compress one level, a row of blocks per task. Padding past the edge repeats the last column or
// row, so it cannot pull the endpoints toward a color the level does not have.
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
    const uint64_t bytes = blockBytes(blockFormat);

    Platform::parallelFor(blocksHigh, 1, [&](size_t blockY) {
        // 16 RGBA texels per block, the shape bc7e encodes a run of blocks from.
        std::vector<uint32_t> row(static_cast<size_t>(blocksWide) * 16);
        for (uint32_t blockX = 0; blockX < blocksWide; ++blockX) {
            for (uint32_t y = 0; y < 4; ++y) {
                const uint32_t line = std::min(static_cast<uint32_t>(blockY) * 4 + y, height - 1);
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint32_t column = std::min(blockX * 4 + x, width - 1);
                    const uint8_t* texel = texels + (static_cast<size_t>(line) * width + column) * channels;
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
    Color,  ///< sRGB color, decoded to light first; its alpha weights it.
    Data,   ///< Numbers, a channel at a time.
    Normal  ///< A direction: x and y unpacked, z rebuilt, renormalized after.
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

// The sRGB curve, its power the engine's own (PortableMath::root): the C library's pow differs
// between platforms in its last bit, and a mip's bytes with it.
float decodeSrgb(double encoded) {
    if (encoded <= 0.04045) return static_cast<float>(encoded / 12.92);
    const double x       = (encoded + 0.055) / 1.055;
    const double squared = x * x;
    return static_cast<float>(squared * PortableMath::root(squared, 5));  // x^2.4
}

float srgbToLinear(uint8_t value) {
    static const std::array<float, 256> s_table = [] {
        std::array<float, 256> table{};
        for (size_t i = 0; i < table.size(); ++i) table[i] = decodeSrgb(static_cast<double>(i) / 255.0);
        return table;
    }();
    return s_table[value];
}

uint8_t quantize(float value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// The byte whose rounding range holds @p value once encoded: how many of the curve's midpoints between
// two bytes it reaches, so the encode is the decode's inverse and needs no power of its own.
uint8_t linearToSrgb(float value) {
    static const std::array<float, 255> s_midpoints = [] {
        std::array<float, 255> midpoints{};
        for (size_t i = 0; i < midpoints.size(); ++i) {
            midpoints[i] = decodeSrgb((static_cast<double>(i) + 0.5) / 255.0);
        }
        return midpoints;
    }();
    const auto reached = std::upper_bound(s_midpoints.begin(), s_midpoints.end(), value);
    return static_cast<uint8_t>(reached - s_midpoints.begin());
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
        const bool color = filtering == Filtering::Color && (channels < 4 || i % 4 != 3);
        out[i] = color ? srgbToLinear(texels[i]) : texels[i] / 255.0f;
    }
    return out;
}

// A filtered normal is short where directions disagreed; unnormalized, its x and y rebuild a
// tilting z.
void renormalize(std::vector<float>& xyz) {
    for (size_t i = 0; i + 2 < xyz.size(); i += 3) {
        const glm::vec3 n(xyz[i], xyz[i + 1], xyz[i + 2]);
        const float length   = glm::length(n);
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
            xy[i * 2 + 0] = quantize(linear[i * 3 + 0] * 0.5f + 0.5f);
            xy[i * 2 + 1] = quantize(linear[i * 3 + 1] * 0.5f + 0.5f);
        }
        return xy;
    }

    std::vector<uint8_t> out(linear.size());
    for (size_t i = 0; i < linear.size(); ++i) {
        const bool alpha = channels == 4 && i % 4 == 3;
        if (alpha)                              out[i] = quantize(linear[i] * alphaScale);
        else if (filtering == Filtering::Color) out[i] = linearToSrgb(linear[i]);
        else                                    out[i] = quantize(linear[i]);
    }
    return out;
}

/**
 * @brief A normal map's directions averaged, not renormalized, level by level.
 *
 * An average of unit vectors is short by how much they disagree, which is the normal detail a
 * filtered level loses: a level's length is what its texel's footprint spread.
 */
struct NormalPyramid {
    struct Level {
        int                    width  = 0;
        int                    height = 0;
        std::vector<glm::vec3> mean;
    };
    std::vector<Level> levels;
};

// From a normal map's decoded RG8 texels, z rebuilt, each level a 2x2 box of the one above.
NormalPyramid buildNormalPyramid(const TextureAsset& normal) {
    NormalPyramid pyramid;
    const TextureParams& params = normal.params;
    if (params.type != TexturePixelType::UnsignedByte || channelCount(params.format) != 2
        || normal.pixelData.size() != static_cast<size_t>(params.width) * params.height * 2) {
        return pyramid;
    }
    NormalPyramid::Level top;
    top.width  = static_cast<int>(params.width);
    top.height = static_cast<int>(params.height);
    top.mean.resize(static_cast<size_t>(top.width) * top.height);
    for (size_t i = 0; i < top.mean.size(); ++i) {
        const float x = normal.pixelData[i * 2 + 0] / 255.0f * 2.0f - 1.0f;
        const float y = normal.pixelData[i * 2 + 1] / 255.0f * 2.0f - 1.0f;
        top.mean[i] = glm::vec3(x, y, std::sqrt(std::max(0.0f, 1.0f - x * x - y * y)));
    }
    pyramid.levels.push_back(std::move(top));
    while (pyramid.levels.back().width > 1 || pyramid.levels.back().height > 1) {
        const NormalPyramid::Level& above = pyramid.levels.back();
        NormalPyramid::Level below;
        below.width  = std::max(above.width / 2, 1);
        below.height = std::max(above.height / 2, 1);
        below.mean.resize(static_cast<size_t>(below.width) * below.height);
        for (int y = 0; y < below.height; ++y) {
            for (int x = 0; x < below.width; ++x) {
                glm::vec3 sum(0.0f);
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        const int sx = std::min(x * 2 + dx, above.width - 1);
                        const int sy = std::min(y * 2 + dy, above.height - 1);
                        sum += above.mean[static_cast<size_t>(sy) * above.width + sx];
                    }
                }
                below.mean[static_cast<size_t>(y) * below.width + x] = sum * 0.25f;
            }
        }
        pyramid.levels.push_back(std::move(below));
    }
    return pyramid;
}

/**
 * @brief Raise a metallic-roughness level's roughness (G) by the normal detail under each texel.
 *
 * Toksvig's idea in the von Mises-Fisher form (Neubelt and Pettineo): a mean
 * normal of length l spreads as kappa = (3l - l^3) / (1 - l^2), and a GGX lobe widened by it has
 * alpha^2 + 2 / kappa. Read from the normal level whose texel matches this one's footprint, so a
 * distant glossy surface whose bumps the mips average away is as rough as they made it, rather
 * than a mirror that sparkles where they line up.
 */
void foldNormalVariance(
    std::vector<float>& level,
    int width,
    int height,
    uint32_t channels,
    const NormalPyramid& pyramid
) {
    if (pyramid.levels.empty() || channels < 2) return;
    const NormalPyramid::Level& top = pyramid.levels.front();
    // The normal level spanning one of this level's texels.
    const float span = std::max(static_cast<float>(top.width) / static_cast<float>(width), 1.0f);
    const int   last = static_cast<int>(pyramid.levels.size()) - 1;
    // The whole log2 nearest span, up to last: 2^(pick - 1/2) <= span < 2^(pick + 1/2), squared so
    // the test is exact, where the C library's log2 is not the same everywhere.
    const double spanSquared = static_cast<double>(span) * static_cast<double>(span);
    int pick = 0;
    while (pick < last && spanSquared >= std::ldexp(1.0, 2 * pick + 1)) ++pick;
    if (pick <= 0) return;
    const NormalPyramid::Level& source = pyramid.levels[static_cast<size_t>(pick)];
    for (int y = 0; y < height; ++y) {
        const int sy = std::min(y * source.height / std::max(height, 1), source.height - 1);
        for (int x = 0; x < width; ++x) {
            const int sx = std::min(x * source.width / std::max(width, 1), source.width - 1);
            const glm::vec3 mean = source.mean[static_cast<size_t>(sy) * source.width + sx];
            // 2 / kappa, written so that normals that all agree add nothing.
            const float l        = glm::clamp(glm::length(mean), glm::epsilon<float>(), 1.0f);
            const float variance = 2.0f * (1.0f - l * l) / (l * (3.0f - l * l));
            float& rough = level[(static_cast<size_t>(y) * width + x) * channels + 1];
            const float alpha = rough * rough;
            rough = std::min(std::sqrt(std::sqrt(alpha * alpha + variance)), 1.0f);
        }
    }
}

/**
 * @brief Filter a texture's mip chain: level 0 the decoded source, each level below from the one above.
 *
 * @param source    The decoded texture, its pixel bytes checked.
 * @param filtering How its texels filter.
 * @param pyramid   A paired normal map's, folded into each stored level; empty for none.
 * @param chain     Sized to the chain's levels; each is filled with its stored texels.
 * @return False, having logged, when a level could not be filtered.
 */
bool filterChain(
    const TextureAsset& source,
    Filtering filtering,
    const NormalPyramid& pyramid,
    std::vector<std::vector<uint8_t>>& chain
) {
    const TextureParams& params = source.params;
    const uint32_t channels = channelCount(params.format);
    const uint32_t levels   = static_cast<uint32_t>(chain.size());
    // A normal is filtered as x, y and z, and stored as x and y.
    const uint32_t filtered = filtering == Filtering::Normal ? 3 : channels;
    // Each level keeps level 0's share of texels above the alpha cutoff.
    const bool coverage = filtering == Filtering::Color && channels == 4;

    chain[0] = source.pixelData;
    if (!pyramid.levels.empty()) {
        std::vector<float> top = toLinear(source.pixelData, channels, filtering);
        const int topWidth     = static_cast<int>(params.width);
        const int topHeight    = static_cast<int>(params.height);
        foldNormalVariance(top, topWidth, topHeight, channels, pyramid);
        chain[0] = toStored(top, channels, filtering, 1.0f);
    }

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
            filterLayoutOf(filtered, isSrgbFormat(params.internalFormat)),
            STBIR_TYPE_FLOAT,
            edgeOf(params.wrapS),
            STBIR_FILTER_DEFAULT
        );
        if (!resized) {
            LOG_ERROR("Texture '%s': mip level %u could not be filtered", source.name().c_str(), level);
            return false;
        }
        if (filtering == Filtering::Normal) renormalize(below);
        const float alphaScale = coverage ? coverageScale(below, targetCoverage) : 1.0f;
        if (pyramid.levels.empty()) {
            chain[level] = toStored(below, channels, filtering, alphaScale);
        } else {
            std::vector<float> folded = below;
            foldNormalVariance(folded, width, height, channels, pyramid);
            chain[level] = toStored(folded, channels, filtering, alphaScale);
        }
        above = std::move(below);
    }
    return true;
}

/**
 * @brief Store a filtered chain in @p out, block-compressed unless it is too small or filtered nearest.
 *
 * @param source The texture the chain was filtered from.
 * @param chain  filterChain's levels.
 * @param out    Takes the layout and the bytes.
 */
void storeChain(
    const TextureAsset& source,
    const std::vector<std::vector<uint8_t>>& chain,
    TextureAsset& out
) {
    const TextureParams& params = source.params;
    const uint32_t levels = static_cast<uint32_t>(chain.size());
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
    const Bc7Params bc7 = bc7Params(isSrgbFormat(params.internalFormat));

    uint8_t* write = out.pixelData.data();
    for (uint32_t level = 0; level < levels; ++level) {
        const uint64_t bytes = textureLevelBytes(out.params, level);
        if (compress) {
            encodeLevel(
                chain[level].data(),
                mipExtent(params.width, level),
                mipExtent(params.height, level),
                channelCount(params.format),
                out.params.internalFormat,
                bc7,
                write
            );
        } else {
            std::memcpy(write, chain[level].data(), static_cast<size_t>(bytes));
        }
        write += bytes;
    }
}

} // namespace

bool bakeTexture(const TextureAsset& source, TextureAsset& out, const TextureAsset* roughnessNormal) {
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

    const Filtering filtering = source.usage() == TextureUsage::Normal && channels == 2
        ? Filtering::Normal
        : isSrgbFormat(params.internalFormat) ? Filtering::Color : Filtering::Data;

    // A metallic-roughness map paired with a normal map: its stored levels take the normal
    // detail their texels average away. The levels filtered from keep the plain roughness, so the
    // variance is not counted again at every level below.
    const NormalPyramid pyramid = roughnessNormal && filtering == Filtering::Data && channels >= 2
        ? buildNormalPyramid(*roughnessNormal)
        : NormalPyramid{};

    const uint32_t levels = params.generateMipmaps ? mipChainLength(params.width, params.height) : 1;
    std::vector<std::vector<uint8_t>> chain(levels);
    if (!filterChain(source, filtering, pyramid, chain)) return false;
    storeChain(source, chain, out);
    return true;
}

} // namespace Vkm::Engine::AssetCooker
