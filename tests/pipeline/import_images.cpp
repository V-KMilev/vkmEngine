// Images, folders of maps and sounds imported as the editor imports them: what each file's texels
// mean is the import's to state, and its recipe rebuilds it the same.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "import/audio_import.h"
#include "import/material_import.h"
#include "import/recipe_registration.h"
#include "import/texture_import.h"
#include "io/asset/recipe/asset_factory.h"
#include "resource/asset/material_asset.h"
#include "resource/resource_manager.h"
#include "system/async_loader/async_loader_system.h"

#include "harness/check.h"
#include "harness/world.h"
#include "pipeline/art.h"

using namespace Vkm::Audio;
using namespace Vkm::Test;

namespace {

/// Eight texels of @p channels each, every byte its own value.
std::vector<uint8_t> texels(int channels) {
    std::vector<uint8_t> out(static_cast<size_t>(8 * channels));
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<uint8_t>(17 + i * 29);
    return out;
}

/// @p from's texels as @p channels: gray widened to color, color narrowed to x and y; rows from the
/// bottom up, as they are stored.
std::vector<uint8_t> stored(const std::vector<uint8_t>& from, int fileChannels, int channels) {
    std::vector<uint8_t> out;
    for (size_t row = 2; row-- > 0;) {
        for (size_t t = row * 4; t < row * 4 + 4; ++t) {
            const uint8_t* texel = &from[t * fileChannels];
            uint8_t rgba[4] = {texel[0], texel[0], texel[0], 255};
            if (fileChannels == 2) rgba[3] = texel[1];
            if (fileChannels >= 3) rgba[1] = texel[1], rgba[2] = texel[2];
            if (fileChannels == 4) rgba[3] = texel[3];
            out.insert(out.end(), rgba, rgba + channels);
        }
    }
    return out;
}

} // namespace

TEST("png, jpg, tga and bmp files import as color, data or a normal, each stored as it means") {
    const auto root = openProject();
    struct File {
        const char* ref;
        int         channels;
    };
    const std::vector<File> files = {
        {"assets/gray.png", 1}, {"assets/gray_alpha.png", 2}, {"assets/rgb.png", 3}, {"assets/rgba.png", 4},
        {"assets/rgb.tga", 3},  {"assets/rgba.tga", 4},       {"assets/rgb.bmp", 3},
    };
    for (const File& file : files) writeImage(root / file.ref, 4, 2, file.channels, texels(file.channels));

    struct Way {
        TextureUsage          usage;
        int                   channels[5];  // stored for a file of 0..4 channels
        TextureInternalFormat formats[5];
    };
    using F = TextureInternalFormat;
    const std::vector<Way> ways = {
        // No one-channel sRGB format, and gray and alpha in RG would sample no alpha.
        {TextureUsage::Color, {0, 4, 4, 3, 4}, {F::Count, F::SRGBA8, F::SRGBA8, F::SRGB8, F::SRGBA8}},
        {TextureUsage::Data, {0, 1, 4, 3, 4}, {F::Count, F::R8, F::RGBA8, F::RGB8, F::RGBA8}},
        // x and y; z is rebuilt where it is sampled.
        {TextureUsage::Normal, {0, 2, 2, 2, 2}, {F::Count, F::RG8, F::RG8, F::RG8, F::RG8}},
    };
    for (const File& file : files) {
        for (const Way& way : ways) {
            std::printf("  %s as %d\n", file.ref, static_cast<int>(way.usage));
            ResourceManager resources;  // a path is one texture a graph: each way in its own
            const TextureHandle handle = loadTexture(file.ref, resources, way.usage, false);
            REQUIRE(handle);
            const TextureAsset& texture = resources.get(handle);
            const int kept = way.channels[file.channels];
            CHECK(texture.name() == file.ref);
            CHECK(texture.params.width == 4);
            CHECK(texture.params.height == 2);
            CHECK(texture.params.internalFormat == way.formats[file.channels]);
            CHECK(texture.usage() == way.usage);
            CHECK(texture.isSrgb() == (way.usage == TextureUsage::Color));
            // Gray read as a normal is gray in x and y.
            const std::vector<uint8_t> source = texels(file.channels);
            CHECK(texture.pixelData == stored(source, file.channels, kept));
            CHECK(findTextureFault(texture).empty());
        }
    }

    // A jpg's color comes back within its rounding.
    std::vector<uint8_t> flat;
    for (int t = 0; t < 8; ++t) flat.insert(flat.end(), {200, 100, 50});
    writeImage(root / "assets/flat.jpg", 4, 2, 3, flat);
    ResourceManager resources;
    const TextureHandle jpg = loadTexture("assets/flat.jpg", resources, TextureUsage::Color, false);
    REQUIRE(jpg);
    CHECK(resources.get(jpg).params.internalFormat == TextureInternalFormat::SRGB8);
    for (size_t i = 0; i < resources.get(jpg).pixelData.size(); ++i) {
        const int want = i % 3 == 0 ? 200 : i % 3 == 1 ? 100 : 50;
        CHECK(std::abs(resources.get(jpg).pixelData[i] - want) <= 2);
    }
}

TEST("a texture asked for again another way is handed back as it was loaded, and the ask said") {
    const auto root = openProject();
    writeImage(root / "assets/map.png", 4, 2, 3, texels(3));
    ResourceManager resources;
    const TextureHandle color = loadTexture("assets/map.png", resources, TextureUsage::Color);
    const TextureHandle again = loadTexture("assets/map.png", resources, TextureUsage::Data);
    CHECK(again == color);
    CHECK(resources.get(color).usage() == TextureUsage::Color);
    CHECK(resources.countOfType<TextureAsset>() == 1);
    CHECK(countInLog("is already loaded as Color and is being asked for as Data") == 1);
    // One named two ways, relative and absolute, is still one.
    CHECK(loadTexture((root / "assets/map.png").string(), resources, TextureUsage::Color) == color);
    CHECK(countInLog("already loaded") == 1);

    CHECK(!loadTexture("assets/none.png", resources, TextureUsage::Color));
    writeText(root / "assets/bad.png", "not an image");
    CHECK(!loadTexture("assets/bad.png", resources, TextureUsage::Color));
    CHECK(countInLog("Failed to load texture") == 2);
    CHECK(resources.countOfType<TextureAsset>() == 1);
}

TEST("a texture's wrap and filter ride in its recipe, and the recipe rebuilds it the same") {
    const auto root = openProject();
    registerRecipeAssetFactories();
    writeImage(root / "assets/tile.png", 4, 2, 4, texels(4));
    ResourceManager resources;
    const TextureHandle tile = loadTexture(
        "assets/tile.png",
        resources,
        TextureUsage::Color,
        true,
        TextureFilterOverride::Nearest,
        TextureWrapMode::Repeat
    );
    REQUIRE(tile);
    const TextureAsset& made = resources.get(tile);
    REQUIRE(made.hasSource());

    ResourceManager other;
    const TextureHandle rebuilt = recipeImport<TextureAsset>()(made.sourceJson(), other);
    REQUIRE(rebuilt);
    REQUIRE(awaitAsyncLoads(other));
    const TextureAsset& again = other.get(rebuilt);
    CHECK(again.params.wrapS == TextureWrapMode::Repeat);
    CHECK(again.params.wrapT == TextureWrapMode::Repeat);
    CHECK(again.params.filterOverride == TextureFilterOverride::Nearest);
    CHECK(again.params.generateMipmaps);
    CHECK(again.usage() == TextureUsage::Color);
    CHECK(again.pixelData == made.pixelData);

    // A plain one says neither, and comes back clamped and filtered as the scene says.
    ResourceManager third;
    const TextureHandle plain = loadTexture("assets/tile.png", third, TextureUsage::Data, false);
    ResourceManager fourth;
    const TextureHandle plainAgain = recipeImport<TextureAsset>()(third.get(plain).sourceJson(), fourth);
    REQUIRE(awaitAsyncLoads(fourth));
    CHECK(fourth.get(plainAgain).params.wrapS == TextureWrapMode::ClampToEdge);
    CHECK(fourth.get(plainAgain).params.filterOverride == TextureFilterOverride::None);
    CHECK(!fourth.get(plainAgain).params.generateMipmaps);
    CHECK(fourth.get(plainAgain).usage() == TextureUsage::Data);
}

TEST("a folder of color, normal and roughness maps becomes one material, each map read as it means") {
    const auto root = openProject();
    for (const char* name : {"brick_Color.png", "brick_Normal.png", "brick_Roughness.png", "brick_AO.png"}) {
        writeImage(root / "assets/brick" / name, 4, 2, 3, texels(3));
    }
    writeText(root / "assets/brick/notes.txt", "not a map");
    ResourceManager resources;
    const MaterialHandle brick = loadMaterialFromFolder("assets/brick", resources);
    REQUIRE(brick);
    REQUIRE(awaitAsyncLoads(resources));
    const MaterialAsset& m = resources.get(brick);
    CHECK(m.name() == "assets/brick");

    const auto map = [&](TextureHandle handle) {
        return resources.isAlive(handle) ? resources.get(handle).name() : std::string();
    };
    CHECK(map(m.albedoTexture) == "assets/brick/brick_Color.png");
    CHECK(map(m.normalTexture) == "assets/brick/brick_Normal.png");
    CHECK(map(m.roughnessTexture) == "assets/brick/brick_Roughness.png");
    CHECK(map(m.aoTexture) == "assets/brick/brick_AO.png");
    // "orm" sits inside "normal": the normal map is no packed occlusion-roughness-metal map.
    CHECK(!m.aoMetallicRoughnessTexture);
    CHECK(!m.metallicTexture);
    CHECK(!m.emissionTexture);
    CHECK(resources.get(m.albedoTexture).usage() == TextureUsage::Color);
    CHECK(resources.get(m.normalTexture).usage() == TextureUsage::Normal);
    CHECK(resources.get(m.roughnessTexture).usage() == TextureUsage::Data);
    // A found map's factor is 1, so the map alone says; no metal map, no metal.
    CHECK(m.roughness == 1.0f);
    CHECK(m.metallic == 0.0f);

    // The folder is the material's identity.
    CHECK(loadMaterialFromFolder("assets/brick", resources) == brick);
    CHECK(loadMaterialFromFolder((root / "assets/brick").string(), resources) == brick);
    CHECK(resources.countOfType<MaterialAsset>() == 1);
    CHECK(resources.countOfType<TextureAsset>() == 4);
    CHECK(!loadMaterialFromFolder("assets/none", resources));
    CHECK(countInLog("Material folder not found: 'assets/none'") == 1);
}

TEST("a wav imports at its own rate and channels, sample for sample, and one that does not decode adds nothing") {
    const auto root = openProject();
    std::vector<int16_t> samples(2 * 300);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<int16_t>((i % 2 ? -1 : 1) * static_cast<int>(i * 37 % 20000));
    }
    writeWav(root / "assets/chime.wav", 22050, 2, samples);
    writeText(root / "assets/noise.wav", "RIFF but not really");

    ResourceManager resources;
    const AudioClipHandle chime = loadAudioClip("assets/chime.wav", resources);
    REQUIRE(chime);
    const AudioClipAsset& clip = resources.get(chime);
    CHECK(clip.name() == "assets/chime.wav");
    CHECK(clip.sampleRate == 22050);
    CHECK(clip.channels == 2);
    REQUIRE(clip.samples);
    CHECK(*clip.samples == samples);
    CHECK(findClipFault(clip).empty());

    CHECK(!loadAudioClip("assets/noise.wav", resources));
    CHECK(!loadAudioClip("assets/missing.wav", resources));
    CHECK(resources.countOfType<AudioClipAsset>() == 1);
    CHECK(countInLog("Failed to load sound") == 2);
}
