// What the cook makes of a texture: its whole mip chain, block-compressed where it can be, each
// level holding up at range - a cut-out as dense, a normal still a direction, roughness as rough
// as the bumps it can no longer show.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "cook/texture/texture_bake.h"
#include "io/asset/cooked/asset_cook.h"
#include "resource/asset/texture_asset.h"

#include "harness/check.h"
#include "harness/scratch.h"

using namespace Vkm::Engine;
using namespace Vkm::Test;

namespace {

/// A decoded texture of one level, as an import leaves it.
TextureAsset decoded(
    uint32_t width,
    uint32_t height,
    TextureInternalFormat format,
    std::vector<uint8_t> pixels,
    TextureFilterOverride filter = TextureFilterOverride::None
) {
    TextureAsset texture;
    texture.params.width          = width;
    texture.params.height         = height;
    texture.params.internalFormat = format;
    texture.params.format         = inferFormat(static_cast<int>(pixels.size() / (width * height)));
    texture.params.wrapS          = TextureWrapMode::Repeat;
    texture.params.wrapT          = TextureWrapMode::Repeat;
    texture.params.filterOverride = filter;
    texture.pixelData             = std::move(pixels);
    return texture;
}

/// Byte offset of level @p level of an uncompressed bake.
size_t levelAt(const TextureAsset& texture, uint32_t level) {
    size_t at = 0;
    for (uint32_t l = 0; l < level; ++l) at += textureLevelBytes(texture.params, l);
    return at;
}

/// A normal tilted @p degrees from +Z toward +X, stored as RG8 bytes.
std::vector<uint8_t> tilted(float degrees) {
    const float r = degrees * 3.14159265f / 180.0f;
    return {static_cast<uint8_t>(std::lround((std::sin(r) * 0.5f + 0.5f) * 255.0f)), 128};
}

/// An 8x8 normal map whose texels lean 10 and 80 degrees in a checker: 45 degrees on average.
TextureAsset bumpy(TextureFilterOverride filter = TextureFilterOverride::Nearest) {
    std::vector<uint8_t> pixels;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const std::vector<uint8_t> n = tilted((x + y) % 2 ? 80.0f : 10.0f);
            pixels.insert(pixels.end(), n.begin(), n.end());
        }
    }
    return decoded(8, 8, TextureInternalFormat::RG8, std::move(pixels), filter);
}

} // namespace

TEST("a baked texture holds its whole mip chain in the block format its texels suit, and reads back the same") {
    struct Case {
        const char*           name;
        TextureInternalFormat from;
        int                   channels;
        TextureInternalFormat to;
    };
    using F = TextureInternalFormat;
    const std::vector<Case> cases = {
        {"color", F::SRGBA8, 4, F::BC7SRGBA}, {"opaque color", F::SRGB8, 3, F::BC7SRGBA},
        {"mask", F::RGBA8, 4, F::BC7RGBA},    {"packed data", F::RGB8, 3, F::BC7RGBA},
        {"gray data", F::R8, 1, F::BC4R},     {"normal", F::RG8, 2, F::BC5RG},
    };
    for (const Case& c : cases) {
        std::printf("  %s\n", c.name);
        std::vector<uint8_t> pixels(16 * 8 * c.channels);
        for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i * 13);
        const TextureAsset source = decoded(16, 8, c.from, pixels);
        TextureAsset baked;
        REQUIRE(AssetCooker::bakeTexture(source, baked));
        CHECK(baked.params.internalFormat == c.to);
        CHECK(baked.params.mipLevels == 5);  // 16x8 down to 1x1
        CHECK(baked.usage() == source.usage());
        CHECK(baked.params.wrapS == TextureWrapMode::Repeat);
        CHECK(findTextureFault(baked).empty());

        const std::filesystem::path path = scratch() / (std::string(c.name) + ".vkmc");
        REQUIRE(AssetCook::writeTexture(path, baked));
        TextureAsset back;
        REQUIRE(AssetCook::readTexture(path, back));
        CHECK(back.params.internalFormat == baked.params.internalFormat);
        CHECK(back.params.mipLevels == baked.params.mipLevels);
        CHECK(back.pixelData == baked.pixelData);
    }

    // Nearest is kept by the texel, and so is what is smaller than a block; a texture that asks for
    // no mips keeps one level.
    TextureAsset pixelArt;
    const auto nearest = TextureFilterOverride::Nearest;
    const std::vector<uint8_t> gray(256, 90);
    REQUIRE(AssetCooker::bakeTexture(decoded(8, 8, F::SRGBA8, gray, nearest), pixelArt));
    CHECK(pixelArt.params.internalFormat == F::SRGBA8);
    CHECK(pixelArt.params.mipLevels == 4);
    CHECK(pixelArt.pixelData.size() == (64 + 16 + 4 + 1) * 4);
    TextureAsset tiny;
    REQUIRE(AssetCooker::bakeTexture(decoded(2, 2, F::RGBA8, std::vector<uint8_t>(16, 3)), tiny));
    CHECK(tiny.params.internalFormat == F::RGBA8);
    CHECK(tiny.params.mipLevels == 2);
    TextureAsset flat = decoded(8, 8, F::R8, std::vector<uint8_t>(64, 7));
    flat.params.generateMipmaps = false;
    TextureAsset one;
    REQUIRE(AssetCooker::bakeTexture(flat, one));
    CHECK(one.params.mipLevels == 1);
    CHECK(one.params.internalFormat == F::BC4R);

    // A level of a solid color is that color; pixels that are not the texels they say are refused.
    for (uint32_t level = 0; level < 4; ++level) {
        const size_t at = levelAt(pixelArt, level);
        CHECK(pixelArt.pixelData[at] == 90);
        CHECK(pixelArt.pixelData[at + 3] == 90);
    }
    TextureAsset broken = decoded(8, 8, F::RGBA8, std::vector<uint8_t>(255, 1));
    CHECK(!AssetCooker::bakeTexture(broken, one));
}

TEST("a baked normal map's levels stay directions, and a roughness map takes the bumps they average away") {
    TextureAsset normals;
    REQUIRE(AssetCooker::bakeTexture(bumpy(), normals));
    REQUIRE(normals.params.internalFormat == TextureInternalFormat::RG8);
    REQUIRE(normals.params.mipLevels == 4);
    // Below the top every texel averages the two leans: 45 degrees, a whole direction, not the
    // shorter mean that would tilt back toward flat once z is rebuilt.
    for (uint32_t level = 1; level < 4; ++level) {
        const size_t at = levelAt(normals, level);
        for (uint32_t i = 0; i < mipExtent(8, level) * mipExtent(8, level); ++i) {
            const float x = normals.pixelData[at + i * 2] / 127.5f - 1.0f;
            const float y = normals.pixelData[at + i * 2 + 1] / 127.5f - 1.0f;
            CHECK_NEAR(x, std::sin(3.14159265f / 4.0f), 0.015f);
            CHECK_NEAR(y, 0.0f, 0.01f);
        }
    }

    // Smooth (G = 51) metallic-roughness texels over the bumpy normals.
    using F = TextureInternalFormat;
    const auto nearest = TextureFilterOverride::Nearest;
    const TextureAsset smooth = decoded(8, 8, F::RGB8, std::vector<uint8_t>(8 * 8 * 3, 51), nearest);
    const TextureAsset flat   = decoded(8, 8, F::RG8, std::vector<uint8_t>(8 * 8 * 2, 128));
    const TextureAsset bumps  = bumpy(TextureFilterOverride::None);
    TextureAsset alone, overFlat, overBumps;
    REQUIRE(AssetCooker::bakeTexture(smooth, alone));
    REQUIRE(AssetCooker::bakeTexture(smooth, overFlat, &flat));
    REQUIRE(AssetCooker::bakeTexture(smooth, overBumps, &bumps));
    CHECK(overFlat.pixelData == alone.pixelData);
    for (uint32_t l = 0; l < 4; ++l) {
        const size_t at = levelAt(overBumps, l);
        // The top's texels each sit on one normal, which hides nothing.
        if (l == 0) CHECK(overBumps.pixelData[at + 1] == 51);
        if (l > 0) CHECK(overBumps.pixelData[at + 1] > 51 + 20);
        CHECK(overBumps.pixelData[at] == 51);      // metal (B) and occlusion (R) as they were
        CHECK(overBumps.pixelData[at + 2] == 51);
    }
}

TEST("a cut-out keeps as much of it standing at every level as at the top") {
    // Two-texel cells, one in three opaque: filtered plainly, every far level drops under the cutoff.
    std::vector<uint8_t> pixels;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const bool opaque = (x / 2 + y / 2) % 3 == 0;
            pixels.insert(pixels.end(), {uint8_t(x * 8), uint8_t(y * 8), 60, uint8_t(opaque ? 255 : 0)});
        }
    }
    const auto nearest = TextureFilterOverride::Nearest;
    const TextureAsset leaves = decoded(32, 32, TextureInternalFormat::SRGBA8, pixels, nearest);
    TextureAsset baked;
    REQUIRE(AssetCooker::bakeTexture(leaves, baked));
    const auto coverage = [&](uint32_t level) {
        const size_t at = levelAt(baked, level);
        const uint32_t count = mipExtent(32, level) * mipExtent(32, level);
        uint32_t standing = 0;
        for (uint32_t i = 0; i < count; ++i) standing += baked.pixelData[at + i * 4 + 3] >= 128;
        return static_cast<float>(standing) / count;
    };
    const float top = coverage(0);
    CHECK_NEAR(top, 1.0f / 3.0f, 0.02f);
    for (uint32_t level = 1; level <= 3; ++level) {
        std::printf("  level %u: %.3f\n", level, coverage(level));
        CHECK_NEAR(coverage(level), top, 0.07f);
    }
    // Color with no alpha to keep is left as the filter makes it.
    TextureAsset opaque;
    const std::vector<uint8_t> plain(192, 200);
    REQUIRE(AssetCooker::bakeTexture(decoded(8, 8, TextureInternalFormat::SRGB8, plain, nearest), opaque));
    CHECK(opaque.pixelData[levelAt(opaque, 3)] == 200);
}
