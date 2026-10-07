#include "support.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <meshoptimizer.h>
#include <nlohmann/json.hpp>

#include "core/fnv1a.h"
#include "debug/engine_error_log.h"
#include "ecs/scene.h"
#include "cook/asset_cooker.h"
#include "cook/bc4_encoder.h"
#include "cook/cook_key.h"
#include "cook/lod_generator.h"
#include "cook/mesh_processing.h"
#include "cook/texture_bake.h"
#include "import/model_source.h"
#include "io/asset/asset_cook.h"
#include "io/asset/asset_factory.h"
#include "io/asset/asset_library.h"
#include "io/asset/asset_serializer.h"
#include "io/asset/cooked_loader.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset_source_kind.h"
#include "resource/generate/mesh_generators.h"

namespace {

// A shipped game reads the cooked format; a developer's machine always has source art
// to fall back on, so a defect here is invisible until it is on somebody else's disk.

std::filesystem::path scratch(const char* name) {
    return runScratch() / name;
}

MeshAsset aSkinnedMesh() {
    MeshAsset mesh;
    mesh.vertices = {
        Vertex{{0, 0, 0}, {0, 1, 0}, {0.0f, 0.0f}, {1, 0, 0, 1}},
        Vertex{{1, 0, 0}, {0, 1, 0}, {1.0f, 0.0f}, {1, 0, 0, -1}},
        Vertex{{0, 0, 1}, {0, 1, 0}, {0.0f, 1.0f}, {0, 0, 1, 1}},
    };
    mesh.indices    = {0, 1, 2};
    // unorm8 influences summing to exactly 255: the skinning shader divides by that
    // rather than renormalising per vertex.
    mesh.skin       = {
        SkinVertex{{0, 1, 0, 0}, {191, 64, 0, 0}},
        SkinVertex{{2, 0, 0, 0}, {255, 0, 0, 0}},
        SkinVertex{{1, 2, 0, 0}, {128, 127, 0, 0}}
    };
    mesh.skeleton   = "rig:bot";
    mesh.boundsMin  = {0.0f, 0.0f, 0.0f};
    mesh.boundsMax  = {1.0f, 0.0f, 1.0f};
    mesh.skinRadius = 2.5f;
    return mesh;
}

void testAMeshSurvivesTheRoundTrip() {
    std::printf("A cooked mesh read back:\n");

    const std::filesystem::path path = scratch("vkm_cook_mesh.vkmc");
    const MeshAsset written = aSkinnedMesh();
    check("it writes", AssetCook::writeMesh(path, written));

    MeshAsset read;
    check("and reads", AssetCook::readMesh(path, read));

    check("every vertex comes back", read.vertices.size() == written.vertices.size());
    check("  with its position", !read.vertices.empty() && nearly(read.vertices[2].position.z, 1.0f));
    check("  its uv", !read.vertices.empty() && nearly(read.vertices[1].uv.x, 1.0f));
    check(
        "  and the handedness in the tangent's w",
        !read.vertices.empty() && nearly(read.vertices[1].tangent.w, -1.0f)
    );
    check("the indices come back", read.indices == written.indices);
    check(
        "the skin weights come back",
        read.skin.size() == 3
            && read.skin[0].weights[0] == 191
            && read.skin[0].weights[1] == 64
            && read.skin[0].bones[1] == 1
    );
    check(
        "  still summing to 255, which the shader divides by",
        read.skin.size() == 3 && read.skin[2].weights[0] + read.skin[2].weights[1] == 255
    );
    check("the rig it addresses comes back by name", read.skeleton == written.skeleton);
    check("the bounds come back", nearly(read.boundsMax.x, 1.0f) && nearly(read.boundsMin.x, 0.0f));
    check("and the skin radius, which nothing recomputes", nearly(read.skinRadius, 2.5f));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void testAnUnskinnedMeshCarriesNoSkin() {
    std::printf("A cooked mesh with no rig:\n");

    const std::filesystem::path path = scratch("vkm_cook_static.vkmc");
    MeshAsset written = aSkinnedMesh();
    written.skin.clear();
    written.skeleton.clear();
    check("it writes", AssetCook::writeMesh(path, written));

    MeshAsset read;
    check("and reads", AssetCook::readMesh(path, read));
    check("  with its geometry", read.vertices.size() == 3 && read.indices.size() == 3);
    check("  and no skin invented for it", read.skin.empty() && read.skeleton.empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void testASkeletonAndAClipSurviveTheRoundTrip() {
    std::printf("A cooked skeleton and the clip that addresses it:\n");

    const std::filesystem::path bones = scratch("vkm_cook_skel.vkmc");
    SkeletonAsset skeleton;
    skeleton.bones = { Bone{"hips", -1}, Bone{"spine", 0} };
    skeleton.inverseBind = { glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), {0, 1, 0}) };
    Transform hips;
    Transform spine;
    spine.position = {0.0f, 1.0f, 0.0f};
    skeleton.bindPose = { hips, spine };
    check("the skeleton writes", AssetCook::writeSkeleton(bones, skeleton));

    SkeletonAsset readBones;
    check("and reads", AssetCook::readSkeleton(bones, readBones));
    check("  with its bones named", readBones.bones.size() == 2 && readBones.bones[1].name == "spine");
    check("  and parented", readBones.bones.size() == 2 && readBones.bones[1].parent == 0);
    check(
        "  its inverse binds",
        readBones.inverseBind.size() == 2 && nearly(readBones.inverseBind[1][3][1], 1.0f)
    );
    check(
        "  and the bind pose the retarget needs",
        readBones.bindPose.size() == 2 && nearly(readBones.bindPose[1].position.y, 1.0f)
    );

    // The writer judges by the reader's rule, so whatever it writes the reader takes.
    SkeletonAsset backwards = skeleton;
    backwards.bones[0].parent = 1;
    check(
        "a skeleton whose parent comes after its child is refused a cook",
        !AssetCook::writeSkeleton(scratch("vkm_cook_skel_backwards.vkmc"), backwards)
    );

    const std::filesystem::path clipPath = scratch("vkm_cook_clip.vkmc");
    AnimationClipAsset clip;
    clip.skeleton      = "rig:bot";
    clip.duration      = 1.5f;
    clip.positionTimes = {0.0f, 0.75f, 1.5f};
    clip.positions     = {{0, 0, 0}, {0, 1, 0}, {0, 0, 0}};
    clip.bones.resize(2);
    clip.bones[1].position = {0, 3};
    clip.markers.push_back(ClipMarker{"footfall", 0.75f});
    check("the clip writes", AssetCook::writeAnimationClip(clipPath, clip));

    AnimationClipAsset readClip;
    check("and reads", AssetCook::readAnimationClip(clipPath, readClip));
    check("  keeping the rig it is authored against", readClip.skeleton == "rig:bot");
    check("  its duration", nearly(readClip.duration, 1.5f));
    check("  its keys", readClip.positions.size() == 3 && nearly(readClip.positions[1].y, 1.0f));
    check(
        "  the channel span each bone owns",
        readClip.bones.size() == 2 && readClip.bones[1].position.count == 3
    );
    check(
        "  and the markers gameplay listens for",
        readClip.markers.size() == 1 && readClip.markers[0].name == "footfall"
    );

    std::error_code ec;
    std::filesystem::remove(bones, ec);
    std::filesystem::remove(clipPath, ec);
}

void testATextureAndASoundSurviveTheRoundTrip() {
    std::printf("A cooked texture and sound:\n");

    const std::filesystem::path texPath = scratch("vkm_cook_tex.vkmc");
    TextureAsset texture;
    texture.params.width  = 2;
    texture.params.height = 2;
    texture.params.format = TexturePixelFormat::RGBA;
    texture.params.type   = TexturePixelType::UnsignedByte;
    texture.params.generateMipmaps = true;
    texture.params.internalFormat  = TextureInternalFormat::SRGBA8;
    // Four texels of four bytes: the reader refuses pixels that are not exactly the
    // levels the params describe.
    texture.pixelData.resize(16);
    for (uint8_t i = 0; i < 16; ++i) texture.pixelData[i] = static_cast<uint8_t>(i + 1);
    check("the texture writes", AssetCook::writeTexture(texPath, texture));

    TextureAsset readTex;
    check("and reads", AssetCook::readTexture(texPath, readTex));
    check("  at its size", readTex.params.width == 2 && readTex.params.height == 2);
    check("  with its pixels", readTex.pixelData == texture.pixelData);
    check(
        "  and the two things a decode cannot re-derive",
        readTex.params.internalFormat == TextureInternalFormat::SRGBA8 && readTex.params.generateMipmaps
    );
    check("  with the colour space read off the format it is stored in", readTex.isSrgb());

    const std::filesystem::path wav = scratch("vkm_cook_audio.vkmc");
    AudioClipAsset audio;
    audio.sampleRate = 48000;
    audio.channels   = 2;
    audio.samples    = ClipSamples(std::vector<int16_t>{0, 16384, -16384, 32767});
    check("the sound writes", AssetCook::writeAudioClip(wav, audio));

    AudioClipAsset readAudio;
    check("and reads", AssetCook::readAudioClip(wav, readAudio));
    check("  at its rate and channel count", readAudio.sampleRate == 48000 && readAudio.channels == 2);
    check(
        "  with its samples",
        readAudio.sampleCount() == 4 && readAudio.samples && (*readAudio.samples)[2] == -16384
    );
    check("  and a frame count the two of them agree on", readAudio.frameCount() == 2);

    std::error_code ec;
    std::filesystem::remove(texPath, ec);
    std::filesystem::remove(wav, ec);
}

// A decoded texture whose texels all differ, so a level built from the wrong place shows.
TextureAsset aDecodedTexture(uint32_t width, uint32_t height, TextureInternalFormat format) {
    const int channels = (format == TextureInternalFormat::R8) ? 1
        : (format == TextureInternalFormat::RG8) ? 2 : 4;
    TextureAsset texture;
    texture.params.width          = width;
    texture.params.height         = height;
    texture.params.internalFormat = format;
    texture.params.format         = inferFormat(channels);
    texture.params.type           = TexturePixelType::UnsignedByte;
    texture.params.wrapS          = TextureWrapMode::Repeat;
    texture.params.wrapT          = TextureWrapMode::Repeat;
    texture.pixelData.resize(static_cast<size_t>(width) * height * channels);
    for (size_t i = 0; i < texture.pixelData.size(); ++i) {
        texture.pixelData[i] = static_cast<uint8_t>((i * 37) & 0xFF);
    }
    return texture;
}

uint64_t chainBytes(const TextureParams& params) {
    uint64_t bytes = 0;
    for (uint32_t level = 0; level < params.mipLevels; ++level) bytes += textureLevelBytes(params, level);
    return bytes;
}

void testAMipChainHasALevelPerHalving() {
    std::printf("How many levels a mip chain has, and how big each is:\n");

    check("a single texel is one level", mipChainLength(1, 1) == 1);
    check("2048 square halves eleven times", mipChainLength(2048, 2048) == 12);
    check("the longer side decides", mipChainLength(64, 32) == 7 && mipChainLength(1, 1000) == 10);
    check("an empty image has none", mipChainLength(0, 4) == 0);
    check("a side halves rounding down", mipExtent(1000, 3) == 125 && mipExtent(5, 1) == 2);
    check("  and never below one texel", mipExtent(64, 7) == 1 && mipExtent(64, 40) == 1);

    TextureParams blocks;
    blocks.width          = 5;
    blocks.height         = 3;
    blocks.internalFormat = TextureInternalFormat::BC7RGBA;
    check("a block level rounds each side up to whole blocks", textureLevelBytes(blocks, 0) == 2 * 16);
    check("  so a level below a block is still one", textureLevelBytes(blocks, 2) == 16);
    blocks.internalFormat = TextureInternalFormat::BC4R;
    check("  of eight bytes for BC4", textureLevelBytes(blocks, 0) == 2 * 8);
}

void testACompressedTextureSurvivesTheRoundTrip() {
    std::printf("A texture cooked into blocks and a mip chain:\n");

    TextureAsset source = aDecodedTexture(64, 32, TextureInternalFormat::SRGBA8);
    source.params.generateMipmaps = true;
    TextureAsset baked;
    check("an sRGB colour texture bakes", AssetCooker::bakeTexture(source, baked));
    check(
        "  into sRGB BC7",
        baked.params.internalFormat == TextureInternalFormat::BC7SRGBA && baked.isSrgb()
    );
    check("  carrying every level down to 1x1", baked.params.mipLevels == 7);
    // 64x32 is 16x8 blocks, then 8x4, 4x2, 2x1 and three levels of one block.
    check(
        "  in exactly the bytes those levels take",
        baked.pixelData.size() == chainBytes(baked.params)
            && baked.pixelData.size() == (128 + 32 + 8 + 2 + 3) * 16
    );
    check(
        "  keeping its size and its wrap",
        baked.params.width == 64 && baked.params.height == 32 && baked.params.wrapS == TextureWrapMode::Repeat
    );
    check(
        "and the source keeps the pixels it had",
        source.pixelData.size() == 64 * 32 * 4 && source.params.mipLevels == 1
    );

    const std::filesystem::path path = scratch("vkm_cook_bc7.vkmc");
    check("the baked texture writes", AssetCook::writeTexture(path, baked));
    TextureAsset read;
    check("and reads", AssetCook::readTexture(path, read));
    check(
        "  with its format and its levels",
        read.params.internalFormat == TextureInternalFormat::BC7SRGBA && read.params.mipLevels == 7
    );
    check("  and every block", read.pixelData == baked.pixelData);

    TextureAsset maskSource = aDecodedTexture(16, 16, TextureInternalFormat::R8);
    maskSource.params.generateMipmaps = false;
    TextureAsset mask;
    check("a linear one-channel map bakes", AssetCooker::bakeTexture(maskSource, mask));
    check(
        "  into BC4, eight bytes a block",
        mask.params.internalFormat == TextureInternalFormat::BC4R
            && textureLevelBytes(mask.params, 0) == 16 * 8
    );
    check("  with one level, as its recipe asked", mask.params.mipLevels == 1);

    TextureAsset twoChannel;
    check(
        "a two-channel map bakes",
        AssetCooker::bakeTexture(aDecodedTexture(8, 8, TextureInternalFormat::RG8), twoChannel)
    );
    check("  into BC5", twoChannel.params.internalFormat == TextureInternalFormat::BC5RG);

    TextureAsset linear;
    check(
        "a linear colour texture bakes",
        AssetCooker::bakeTexture(aDecodedTexture(8, 8, TextureInternalFormat::RGBA8), linear)
    );
    check(
        "  into linear BC7",
        linear.params.internalFormat == TextureInternalFormat::BC7RGBA && !linear.isSrgb()
    );

    TextureAsset pixelArt = aDecodedTexture(16, 16, TextureInternalFormat::SRGBA8);
    pixelArt.params.filterOverride  = TextureFilterOverride::Nearest;
    pixelArt.params.generateMipmaps = true;
    TextureAsset sharp;
    check("a texture that asked for nearest sampling bakes", AssetCooker::bakeTexture(pixelArt, sharp));
    check(
        "  keeping its texels",
        sharp.params.internalFormat == TextureInternalFormat::SRGBA8
            && sharp.params.mipLevels == 5
            && sharp.pixelData.size() == chainBytes(sharp.params)
    );
    check(
        "  with level 0 untouched",
        sharp.pixelData.size() >= pixelArt.pixelData.size()
            && std::equal(pixelArt.pixelData.begin(), pixelArt.pixelData.end(), sharp.pixelData.begin())
    );

    TextureAsset tiny = aDecodedTexture(2, 2, TextureInternalFormat::SRGBA8);
    tiny.params.generateMipmaps = true;
    TextureAsset small;
    check("a texture smaller than a block bakes", AssetCooker::bakeTexture(tiny, small));
    check(
        "  as texels, with its chain",
        small.params.internalFormat == TextureInternalFormat::SRGBA8 && small.params.mipLevels == 2
    );

    TextureAsset hdr;
    hdr.params.width          = 4;
    hdr.params.height         = 4;
    hdr.params.internalFormat = TextureInternalFormat::RGBA16F;
    hdr.params.type           = TexturePixelType::HalfFloat;
    hdr.pixelData.assign(4 * 4 * 8, 0x3C);
    TextureAsset passed;
    check("a float texture bakes", AssetCooker::bakeTexture(hdr, passed));
    check(
        "  untouched, one level for the upload to mip",
        passed.params.internalFormat == TextureInternalFormat::RGBA16F
            && passed.params.mipLevels == 1
            && passed.pixelData == hdr.pixelData
    );

    TextureAsset wrong = aDecodedTexture(8, 8, TextureInternalFormat::RGBA8);
    wrong.pixelData.pop_back();
    TextureAsset refused;
    check("pixels their params do not describe are refused", !AssetCooker::bakeTexture(wrong, refused));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// A BC4 block decoded as the GPU reads it - two endpoints, then a 3-bit index a texel -
// into every @p stride-th byte of @p out. Written out here, not borrowed from an
// encoder library, so the check does not trust what it checks.
void decodeBC4(const uint8_t* block, uint8_t* out, size_t stride) {
    const int a = block[0];
    const int b = block[1];
    int palette[8] = { a, b };
    for (int i = 1; i < (a > b ? 7 : 5); ++i) {
        palette[i + 1] = a > b ? ((7 - i) * a + i * b) / 7 : ((5 - i) * a + i * b) / 5;
    }
    if (a <= b) {
        palette[6] = 0;
        palette[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    for (int texel = 0; texel < 16; ++texel) {
        out[texel * stride] = static_cast<uint8_t>(palette[(bits >> (3 * texel)) & 7]);
    }
}

void testTheBlocksHoldTheTexelsTheyWereMadeFrom() {
    std::printf("What a compressed level decodes back to:\n");

    TextureAsset ramp;
    ramp.params.width           = 8;
    ramp.params.height          = 8;
    ramp.params.internalFormat  = TextureInternalFormat::RG8;
    ramp.params.format          = TexturePixelFormat::RG;
    ramp.params.generateMipmaps = false;
    ramp.pixelData.resize(8 * 8 * 2);
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            ramp.pixelData[(y * 8 + x) * 2 + 0] = static_cast<uint8_t>(x * 30);
            ramp.pixelData[(y * 8 + x) * 2 + 1] = static_cast<uint8_t>(y * 30);
        }
    }
    TextureAsset baked;
    check(
        "a two-channel ramp bakes",
        AssetCooker::bakeTexture(ramp, baked)
            && baked.params.internalFormat == TextureInternalFormat::BC5RG
            && baked.pixelData.size() == 4 * 16
    );

    int worst = 0;
    for (uint32_t blockY = 0; blockY < 2 && baked.pixelData.size() == 4 * 16; ++blockY) {
        for (uint32_t blockX = 0; blockX < 2; ++blockX) {
            uint8_t decoded[16 * 4] = {};
            const uint8_t* block = &baked.pixelData[(blockY * 2 + blockX) * 16];
            decodeBC4(block,     decoded + 0, 4);
            decodeBC4(block + 8, decoded + 1, 4);
            for (uint32_t y = 0; y < 4; ++y) {
                for (uint32_t x = 0; x < 4; ++x) {
                    const int red   = decoded[(y * 4 + x) * 4 + 0] - static_cast<int>((blockX * 4 + x) * 30);
                    const int green = decoded[(y * 4 + x) * 4 + 1] - static_cast<int>((blockY * 4 + y) * 30);
                    worst = std::max(worst, std::max(std::abs(red), std::abs(green)));
                }
            }
        }
    }
    // A block spans its channel in eight steps, here about thirteen apart, so a texel
    // lands within half of one; a misplaced or wrong-channel block is off by thirty+.
    check("every texel comes back within half a step, in its own place", worst <= 7);
}

// Squared error of @p values against @p block's decode, in 35ths so interpolated
// entries are exact: the GPU decodes them past 8 bits, and rounding here would judge a
// fit by an error the hardware does not make.
long long blockError(const uint8_t* values, const uint8_t* block) {
    const int a = block[0];
    const int b = block[1];
    long long palette[8] = { a * 35LL, b * 35LL };
    for (int i = 1; i < (a > b ? 7 : 5); ++i) {
        palette[i + 1] = a > b ? ((7 - i) * a + i * b) * 5LL : ((5 - i) * a + i * b) * 7LL;
    }
    if (a <= b) {
        palette[6] = 0;
        palette[7] = 255 * 35LL;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    long long error = 0;
    for (int i = 0; i < 16; ++i) {
        const long long difference = palette[(bits >> (3 * i)) & 7] - values[i] * 35LL;
        error += difference * difference;
    }
    return error;
}

// The block an extremes-only encoder writes: the eight-value palette between the
// block's min and max, each texel on its nearest entry. What least squares must beat.
void encodeExtremesOnly(const uint8_t* values, uint8_t* block) {
    const auto [low, high] = std::minmax_element(values, values + 16);
    block[0] = *high;
    block[1] = *low;
    uint8_t palette[16];
    for (uint64_t index = 0; index < 8; ++index) {
        block[2] = block[3] = block[4] = block[5] = block[6] = block[7] = 0;
        uint64_t bits = 0;
        for (int i = 0; i < 16; ++i) bits |= index << (3 * i);
        for (int b = 0; b < 6; ++b) block[2 + b] = static_cast<uint8_t>(bits >> (8 * b));
        decodeBC4(block, palette + index, 0);
    }
    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) {
        uint64_t best = 0;
        for (uint64_t index = 1; index < 8; ++index) {
            if (std::abs(palette[index] - values[i]) < std::abs(palette[best] - values[i])) best = index;
        }
        bits |= best << (3 * i);
    }
    for (int b = 0; b < 6; ++b) block[2 + b] = static_cast<uint8_t>(bits >> (8 * b));
}

void testTheBC4EncoderFitsItsBlocks() {
    std::printf("A BC4 block against the values it was made from:\n");

    uint8_t block[8];
    const uint8_t flat[16] = {77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77};
    AssetCooker::encodeBC4Block(flat, block);
    check("a flat block comes back exact", blockError(flat, block) == 0);

    uint8_t ramp[16];
    for (int i = 0; i < 16; ++i) ramp[i] = static_cast<uint8_t>(i * 17);
    AssetCooker::encodeBC4Block(ramp, block);
    uint8_t decoded[16];
    decodeBC4(block, decoded, 1);
    int worst = 0;
    for (int i = 0; i < 16; ++i) worst = std::max(worst, std::abs(decoded[i] - ramp[i]));
    // Eight entries across the whole range are 36 apart; no value is over half that off.
    check("a full-range ramp lands within half a palette step", worst <= 19);

    // Two exact extremes and a narrow cluster: the eight-value palette spreads across the
    // range, the six-value one spends two on 0 and 255 and the rest on the cluster.
    const uint8_t extremes[16] = {0, 255, 100, 102, 104, 106, 108, 110, 0, 255, 101, 103, 105, 107, 109, 110};
    AssetCooker::encodeBC4Block(extremes, block);
    decodeBC4(block, decoded, 1);
    worst = 0;
    for (int i = 0; i < 16; ++i) worst = std::max(worst, std::abs(decoded[i] - extremes[i]));
    check("exact 0 and 255 beside a cluster take the six-value palette", block[0] <= block[1] && worst <= 2);

    // Least squares starts from the extremes and keeps only improvements, so it is never
    // worse per block, and on noisy blocks markedly better: extremes waste an entry on
    // every outlier.
    std::mt19937 random(1234u);
    long long refined = 0;
    long long extremesOnly = 0;
    bool neverWorse = true;
    for (int b = 0; b < 2000; ++b) {
        uint8_t values[16];
        const int centre = static_cast<int>(random() % 256);
        const int spread = 4 + static_cast<int>(random() % 120);
        for (uint8_t& value : values) {
            const int jittered = centre + static_cast<int>(random() % (2 * spread + 1)) - spread;
            value = static_cast<uint8_t>(std::clamp(jittered, 0, 255));
        }
        uint8_t ours[8];
        uint8_t naive[8];
        AssetCooker::encodeBC4Block(values, ours);
        encodeExtremesOnly(values, naive);
        const long long oursError  = blockError(values, ours);
        const long long naiveError = blockError(values, naive);
        neverWorse = neverWorse && oursError <= naiveError;
        refined      += oursError;
        extremesOnly += naiveError;
    }
    std::printf(
        "      squared error over 2000 noisy blocks: %lld refined, %lld from the extremes\n",
        refined,
        extremesOnly
    );
    check("refinement is never worse than the extremes alone", neverWorse);
    check("  and takes at least a tenth off their error", refined * 10 <= extremesOnly * 9);

    uint8_t texels[32];
    for (int i = 0; i < 16; ++i) {
        texels[i * 2 + 0] = static_cast<uint8_t>(i * 17);
        texels[i * 2 + 1] = static_cast<uint8_t>(255 - i * 17);
    }
    uint8_t pair[16];
    AssetCooker::encodeBC5Block(texels, pair);
    uint8_t rg[32];
    decodeBC4(pair, rg, 2);
    decodeBC4(pair + 8, rg + 1, 2);
    worst = 0;
    for (int i = 0; i < 32; ++i) worst = std::max(worst, std::abs(rg[i] - texels[i]));
    check("a BC5 block holds both channels, each in its own half", worst <= 19);
}

// A level averages light, and an sRGB byte is not light: black and white averaged as
// bytes is 128, which displays as a fifth of the light.
void testAnSrgbTextureIsFilteredInLinearLight() {
    std::printf("A mip level of an sRGB texture:\n");

    TextureAsset blackWhite;
    blackWhite.params.width           = 2;
    blackWhite.params.height          = 1;
    blackWhite.params.internalFormat  = TextureInternalFormat::SRGBA8;
    blackWhite.params.generateMipmaps = true;
    blackWhite.pixelData = {0, 0, 0, 255, 255, 255, 255, 255};

    TextureAsset baked;
    check(
        "black beside white bakes",
        AssetCooker::bakeTexture(blackWhite, baked)
            && baked.params.mipLevels == 2
            && baked.pixelData.size() == 12
    );
    const uint8_t srgbMean = baked.pixelData.size() == 12 ? baked.pixelData[8] : 0;
    check("  to the sRGB byte of half the light, not half the byte", srgbMean >= 180 && srgbMean <= 196);
    check(
        "  with alpha averaged as the plain number it is",
        baked.pixelData.size() == 12 && baked.pixelData[11] == 255
    );

    blackWhite.params.internalFormat = TextureInternalFormat::RGBA8;
    check("the same bytes as data bake", AssetCooker::bakeTexture(blackWhite, baked));
    const uint8_t linearMean = baked.pixelData.size() == 12 ? baked.pixelData[8] : 0;
    check("  to half the number", linearMean >= 120 && linearMean <= 135);
}

// A glossy metallic-roughness map paired with a bumpy normal map: its levels below the top keep
// the roughness the bumps they average away would have spread the highlight to, so a surface at
// range does not shine as a mirror; the top level, whose texels each see one normal, keeps its
// own; and unpaired, nothing moves.
void testARoughnessMapTakesItsNormalMapsLostDetail() {
    std::printf("A roughness map paired with its normal map:\n");

    constexpr uint32_t SIZE  = 16;
    constexpr uint8_t  GLOSS = 26;  // roughness 0.1
    TextureAsset roughness = aDecodedTexture(SIZE, SIZE, TextureInternalFormat::RGBA8);
    roughness.params.generateMipmaps = true;
    roughness.params.filterOverride  = TextureFilterOverride::Nearest;  // stored as bytes, readable
    for (size_t i = 0; i < roughness.pixelData.size(); i += 4) roughness.pixelData[i + 1] = GLOSS;

    // Alternate texels tilted 37 degrees either way along x.
    TextureAsset normal = aDecodedTexture(SIZE, SIZE, TextureInternalFormat::RG8);
    for (uint32_t y = 0; y < SIZE; ++y) {
        for (uint32_t x = 0; x < SIZE; ++x) {
            const size_t i = (static_cast<size_t>(y) * SIZE + x) * 2;
            normal.pixelData[i + 0] = (x % 2 == 0) ? 204 : 51;
            normal.pixelData[i + 1] = 128;
        }
    }

    // A level's mean G.
    const auto meanGreen = [](const TextureAsset& baked, uint32_t level) {
        size_t offset = 0;
        for (uint32_t l = 0; l < level; ++l) {
            offset += static_cast<size_t>(textureLevelBytes(baked.params, l));
        }
        const size_t bytes = static_cast<size_t>(textureLevelBytes(baked.params, level));
        double sum = 0.0;
        for (size_t i = offset; i < offset + bytes; i += 4) sum += baked.pixelData[i + 1];
        return sum / static_cast<double>(bytes / 4);
    };

    TextureAsset plain;
    TextureAsset folded;
    check("it bakes unpaired", AssetCooker::bakeTexture(roughness, plain));
    check("  and paired", AssetCooker::bakeTexture(roughness, folded, &normal));
    std::printf(
        "      level 0 %.1f -> %.1f, level 1 %.1f -> %.1f\n",
        meanGreen(plain, 0),
        meanGreen(folded, 0),
        meanGreen(plain, 1),
        meanGreen(folded, 1)
    );
    check("its top level keeps its own roughness", std::abs(meanGreen(folded, 0) - GLOSS) < 1.0);
    check("  the next is rougher by what its texels averaged", meanGreen(folded, 1) > 2.0 * GLOSS);
    check("unpaired, the next keeps the gloss", std::abs(meanGreen(plain, 1) - GLOSS) < 1.0);

    TextureAsset flat = aDecodedTexture(SIZE, SIZE, TextureInternalFormat::RG8);
    std::fill(flat.pixelData.begin(), flat.pixelData.end(), uint8_t{128});
    TextureAsset smooth;
    check("paired with a flat map", AssetCooker::bakeTexture(roughness, smooth, &flat));
    check("  the next keeps the gloss", std::abs(meanGreen(smooth, 1) - GLOSS) < 1.0);
}

// Averaging two directions gives a vector shorter than one; stored as is, its x and y
// rebuild too large a z and the level tilts flat. Each level is renormalised first.
void testANormalMapsLevelsAreDirections() {
    std::printf("The mip levels of a normal map:\n");

    // (0.6, 0, 0.8) beside (0, 0.6, 0.8) in a checkerboard: each texel below averages
    // to (0.3, 0.3, 0.8), whose direction's x is 0.331 - stored as 170, not 166.
    TextureAsset normals = aDecodedTexture(4, 4, TextureInternalFormat::RG8);
    normals.params.generateMipmaps = true;
    normals.params.filterOverride  = TextureFilterOverride::Nearest;
    const auto encode = [](float v) { return static_cast<uint8_t>(std::lround((v * 0.5f + 0.5f) * 255.0f)); };
    for (uint32_t i = 0; i < 16; ++i) {
        const bool along = ((i % 4) + (i / 4)) % 2 == 0;
        normals.pixelData[i * 2 + 0] = encode(along ? 0.6f : 0.0f);
        normals.pixelData[i * 2 + 1] = encode(along ? 0.0f : 0.6f);
    }
    check("an RG map reads as a normal map", normals.usage() == TextureUsage::Normal);

    TextureAsset baked;
    check(
        "it bakes, keeping x and y alone",
        AssetCooker::bakeTexture(normals, baked)
            && baked.params.internalFormat == TextureInternalFormat::RG8
            && baked.params.mipLevels == 3
    );
    const size_t level1 = 4 * 4 * 2;
    const int x = baked.pixelData.size() > level1 ? baked.pixelData[level1] : 0;
    const int y = baked.pixelData.size() > level1 ? baked.pixelData[level1 + 1] : 0;
    std::printf("      level 1 stores x %d, y %d\n", x, y);
    check(
        "  and its levels hold the direction of the average, renormalised",
        std::abs(x - 170) <= 1 && std::abs(y - 170) <= 1
    );

    TextureAsset compressed = normals;
    compressed.params.filterOverride = TextureFilterOverride::None;
    check(
        "  as BC5 once compressed",
        AssetCooker::bakeTexture(compressed, baked)
            && baked.params.internalFormat == TextureInternalFormat::BC5RG
    );
}

// Foliage is alpha-tested against a cutoff. Filtering averages thin blades into their
// gaps, and below the cutoff they vanish - a tree thins to bare branches with distance.
// Each level's alpha is scaled to keep level 0's share above it.
void testACutOutKeepsItsCoverageAtRange() {
    std::printf("How much of a cut-out each mip level keeps:\n");

    // About a quarter of texels opaque on a clear ground - fine leaves, which filtering
    // smears to a quarter-opaque haze.
    TextureAsset blades = aDecodedTexture(32, 32, TextureInternalFormat::SRGBA8);
    blades.params.generateMipmaps = true;
    blades.params.filterOverride  = TextureFilterOverride::Nearest;
    std::mt19937 scatter(77u);
    for (uint32_t i = 0; i < 32 * 32; ++i) {
        blades.pixelData[i * 4 + 3] = (scatter() % 4 == 0) ? 255 : 0;
    }

    TextureAsset baked;
    check(
        "a colour texture with a cut-out alpha bakes",
        AssetCooker::bakeTexture(blades, baked) && baked.params.mipLevels == 6
    );
    size_t offset = 0;
    bool   kept   = true;
    float  first  = 0.0f;
    for (uint32_t level = 0; level < baked.params.mipLevels; ++level) {
        const uint32_t texels = mipExtent(32, level) * mipExtent(32, level);
        size_t covered = 0;
        for (uint32_t t = 0; t < texels && offset + t * 4 + 3 < baked.pixelData.size(); ++t) {
            if (baked.pixelData[offset + t * 4 + 3] > 127) ++covered;
        }
        const float share = static_cast<float>(covered) / static_cast<float>(texels);
        std::printf("      level %u: %.3f covered\n", level, static_cast<double>(share));
        if (level == 0) first = share;
        // A level of fewer than sixteen texels cannot hold the share closely.
        if (texels >= 16) kept = kept && std::abs(share - first) <= 0.07f;
        offset += texels * 4;
    }
    check("  and every level keeps level 0's share of its texels drawn", kept);

    TextureAsset opaque = aDecodedTexture(16, 16, TextureInternalFormat::SRGBA8);
    opaque.params.generateMipmaps = true;
    opaque.params.filterOverride  = TextureFilterOverride::Nearest;
    for (uint32_t i = 0; i < 16 * 16; ++i) opaque.pixelData[i * 4 + 3] = 255;
    bool solid = AssetCooker::bakeTexture(opaque, baked);
    for (size_t i = 3; solid && i < baked.pixelData.size(); i += 4) solid = baked.pixelData[i] == 255;
    check("an opaque texture stays opaque at every level", solid);
}

// A level filtered from the rounded bytes above inherits their rounding, drifting the
// way the content favours. The filter keeps a wrapping texture's mean, so a chain
// filtered in floats and rounded once per level ends at level 0's rounded mean.
void testAChainIsRoundedOncePerLevel() {
    std::printf("The last level of a chain against the mean of the first:\n");

    // Half a 100/101 checkerboard, whose levels are 100.5 (rounding up), half mostly 100
    // (rounding down): the mean is under 100.5, and per-level rounding drifts past it.
    TextureAsset source = aDecodedTexture(32, 32, TextureInternalFormat::R8);
    source.params.generateMipmaps = true;
    source.params.filterOverride  = TextureFilterOverride::Nearest;
    double sum = 0.0;
    for (uint32_t y = 0; y < 32; ++y) {
        for (uint32_t x = 0; x < 32; ++x) {
            const bool high = y < 16 ? ((x + y) % 2 == 0) : (x % 5 < 2);
            source.pixelData[y * 32 + x] = high ? 101 : 100;
            sum += high ? 101.0 : 100.0;
        }
    }
    const double mean = sum / (32.0 * 32.0);

    TextureAsset baked;
    check(
        "a data map with its chain bakes",
        AssetCooker::bakeTexture(source, baked)
            && baked.params.mipLevels == 6
            && baked.params.internalFormat == TextureInternalFormat::R8
    );
    const uint8_t last = baked.pixelData.empty() ? 0 : baked.pixelData.back();
    std::printf("      level 0 averages %.3f; the 1x1 level is %u\n", mean, last);
    check("  and its last level is that mean, rounded once", last == static_cast<uint8_t>(std::lround(mean)));
}

// The cook makes what the runtime uploads; decoded pixels written straight through
// would cook to the import's size, with GL building levels at every load.
void testTheCookWritesTheBakedTexture() {
    std::printf("A texture through the whole cook:\n");

    const ScratchProject project("vkm_cook_texture");
    const std::string name = "tex:albedo";

    ResourceManager editor;
    TextureAsset albedo = aDecodedTexture(32, 32, TextureInternalFormat::SRGBA8);
    albedo.params.generateMipmaps = true;
    albedo.sourceJson() = {
        {"kind", AssetSourceKind::FILE},
        {"path", "assets/albedo.png"},
        {"usage", "Color"},
        {"generateMipmaps", true}
    };
    const TextureHandle handle = editor.add(std::move(albedo), name);
    check("the cook succeeds", AssetCooker::cookAllAssets(editor));
    std::error_code manifestError;
    check(
        "  writing its manifest with the binaries it indexes, not with the recipes",
        std::filesystem::exists(ProjectPaths::cooked() / "_manifest.json", manifestError)
            && !std::filesystem::exists(ProjectPaths::library() / "_manifest.json", manifestError)
    );

    const AssetRecord* record = AssetLibrary::get().find(AssetType::Texture, name);
    TextureAsset cooked;
    check(
        "the cooked file reads",
        record
            && AssetCook::readTexture(
                AssetLibrary::cookedPath(AssetType::Texture, name, record->recipeHash),
                cooked
            )
    );
    check(
        "  as sRGB BC7 with its whole chain",
        cooked.params.internalFormat == TextureInternalFormat::BC7SRGBA && cooked.params.mipLevels == 6
    );
    check(
        "while the asset in memory keeps its decoded pixels",
        editor.get(handle).pixelData.size() == 32 * 32 * 4
    );

    AssetLibrary::get().remove(AssetType::Texture, name);
}

// The editor's Save and Play cook too, and a full BC7 bake is minutes the editor must
// not wait. A save needs the records; binaries can land after, since a load whose
// cooked file is not current falls back to the recipe.
void testASaveRecordsNowAndBakesInTheBackground() {
    std::printf("A cook that bakes in the background:\n");

    const ScratchProject project("vkm_cook_background");
    const std::string name = "tex:background";

    ResourceManager editor;
    TextureAsset albedo = aDecodedTexture(1024, 1024, TextureInternalFormat::SRGBA8);
    albedo.params.generateMipmaps = true;
    albedo.sourceJson() = {
        {"kind", AssetSourceKind::FILE},
        {"path", "assets/background.png"},
        {"usage", "Color"},
        {"generateMipmaps", true}
    };
    editor.add(std::move(albedo), name);

    const auto started = std::chrono::steady_clock::now();
    check("the cook succeeds", AssetCooker::cookAllAssets(editor, AssetCooker::Bake::InBackground));
    const auto returned = std::chrono::steady_clock::now();
    const AssetRecord* record = AssetLibrary::get().find(AssetType::Texture, name);
    std::error_code ec;
    check(
        "  having recorded the texture",
        record && std::filesystem::exists(AssetLibrary::recipePath(AssetType::Texture, name), ec)
    );
    const std::filesystem::path cooked = record
        ? AssetLibrary::cookedPath(AssetType::Texture, name, record->recipeHash)
        : std::filesystem::path{};
    check("  and returned before the bake is done", !AssetCook::isCookedCurrent(AssetType::Texture, cooked));
    check(
        "a second cook while it bakes finds it under way",
        AssetCooker::cookAllAssets(editor, AssetCooker::Bake::InBackground)
    );

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (!AssetCook::isCookedCurrent(AssetType::Texture, cooked)
        && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto landed = std::chrono::steady_clock::now();
    std::printf(
        "      the cook returned after %.0f ms; the bake landed after %.0f ms\n",
        std::chrono::duration<double, std::milli>(returned - started).count(),
        std::chrono::duration<double, std::milli>(landed - started).count()
    );
    TextureAsset baked;
    check(
        "the bake lands on its own",
        AssetCook::readTexture(cooked, baked)
            && baked.params.internalFormat == TextureInternalFormat::BC7SRGBA
            && baked.params.mipLevels == 11
    );

    AssetLibrary::get().remove(AssetType::Texture, name);
}

// Each level's size follows from its sides and format, so the reader checks them all
// before allocating; bytes for other levels, or a level short, are refused rather than
// handed to an upload that would read past them.
void testTheReaderRefusesLevelsItsBytesDoNotHold() {
    std::printf("A cooked texture whose levels and bytes disagree:\n");

    // 8x8 BC7 with its chain: four blocks, then one each for 4x4, 2x2 and 1x1. Any
    // sixteen bytes are a block as far as the file is concerned.
    TextureAsset blocks;
    blocks.params.width          = 8;
    blocks.params.height         = 8;
    blocks.params.internalFormat = TextureInternalFormat::BC7RGBA;
    blocks.params.mipLevels      = 4;
    blocks.pixelData.assign(4 * 16 + 3 * 16, 0x5A);

    const std::filesystem::path path = scratch("vkm_cook_levels.vkmc");
    check("a whole chain writes", AssetCook::writeTexture(path, blocks));
    TextureAsset read;
    check("  and reads", AssetCook::readTexture(path, read) && read.pixelData == blocks.pixelData);

    TextureAsset shortChain = blocks;
    shortChain.pixelData.resize(shortChain.pixelData.size() - 16);
    check(
        "the writer refuses a chain a level short",
        !AssetCook::writeTexture(scratch("vkm_cook_short.vkmc"), shortChain)
    );
    TextureAsset partial = blocks;
    partial.params.mipLevels = 2;
    partial.pixelData.resize(4 * 16 + 16);
    check(
        "  and a chain that stops before 1x1",
        !AssetCook::writeTexture(scratch("vkm_cook_partial.vkmc"), partial)
    );

    std::string good;
    {
        std::ifstream in(path, std::ios::binary);
        good.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    // magic(4) + sentinel(4) + kind(2) + version(2) + payload(8), then the body: width
    // and height (4 each), six enum bytes, the mipmap flag, level count, pixel byte
    // count, the pixels.
    constexpr size_t PAYLOAD_AT = 12;
    constexpr size_t LEVELS_AT  = 20 + 4 + 4 + 6 + 1;
    constexpr size_t PIXELS_AT  = LEVELS_AT + 4;
    const auto readPatched = [&](const std::string& bytes) {
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        TextureAsset out;
        return AssetCook::readTexture(path, out);
    };
    const auto withLevels = [&](uint32_t levels) {
        std::string bytes = good;
        std::memcpy(&bytes[LEVELS_AT], &levels, sizeof(levels));
        return bytes;
    };
    check("the file patched back as it was still reads", readPatched(withLevels(4)));
    check("a chain read as one level is refused, its bytes left over", !readPatched(withLevels(1)));
    check("a count that is neither one nor the chain is refused", !readPatched(withLevels(3)));

    // The last level cut off, both counts adjusted: the file is self-consistent and a
    // level short of what its count needs.
    std::string cut = good.substr(0, good.size() - 16);
    uint64_t payload = 0;
    uint64_t pixels  = 0;
    std::memcpy(&payload, &cut[PAYLOAD_AT], sizeof(payload));
    std::memcpy(&pixels, &cut[PIXELS_AT], sizeof(pixels));
    payload -= 16;
    pixels  -= 16;
    std::memcpy(&cut[PAYLOAD_AT], &payload, sizeof(payload));
    std::memcpy(&cut[PIXELS_AT], &pixels, sizeof(pixels));
    check("a chain missing its last level is refused", !readPatched(cut));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// A file that is not what it says: a reader trusting a length allocates whatever the
// damage says, and one trusting a count walks off the end of what it read.
void testADamagedFileIsRefusedRatherThanRead() {
    std::printf("A cooked file that is not what it claims:\n");

    const std::filesystem::path path = scratch("vkm_cook_damaged.vkmc");
    check("a whole one reads", AssetCook::writeMesh(path, aSkinnedMesh()));
    MeshAsset ok;
    check("  to start with", AssetCook::readMesh(path, ok));

    const auto sizeOf = [&] {
        std::error_code ec;
        return static_cast<std::streamoff>(std::filesystem::file_size(path, ec));
    };
    const std::streamoff whole = sizeOf();

    // Cut in half: the header still parses and promises a body that is not there.
    {
        std::string bytes;
        {
            std::ifstream in(path, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(whole / 2));
    }
    MeshAsset truncated;
    check("a truncated file is refused", !AssetCook::readMesh(path, truncated));
    check("  and it is not current either", !AssetCook::isCookedCurrent(AssetType::Mesh, path));

    // A different kind under the same name: the header says mesh, the caller asks for a
    // texture.
    check("a whole mesh writes again", AssetCook::writeMesh(path, aSkinnedMesh()));
    TextureAsset asTexture;
    check("reading a mesh as a texture is refused", !AssetCook::readTexture(path, asTexture));
    check("  and a mesh is not current as a texture", !AssetCook::isCookedCurrent(AssetType::Texture, path));
    check("  while it is still current as a mesh", AssetCook::isCookedCurrent(AssetType::Mesh, path));

    // A layout this build does not read, patched by hand. The version rides in the
    // artifact's name, so a bump orphans old files rather than leaving one for this gate.
    {
        // magic(4) + endian sentinel(4) + assetKind(2), then the version.
        constexpr std::streamoff VERSION_OFFSET = 4 + 4 + 2;
        std::fstream patch(path, std::ios::binary | std::ios::in | std::ios::out);
        check("the artifact is open to patch", static_cast<bool>(patch));
        patch.seekp(VERSION_OFFSET);
        const uint16_t fromTheFuture = 0xBEEF;
        patch.write(reinterpret_cast<const char*>(&fromTheFuture), sizeof(fromTheFuture));
    }
    MeshAsset fromTheFuture;
    check("a layout this build does not read is refused", !AssetCook::readMesh(path, fromTheFuture));
    check(
        "  while the staleness probe, which does not look at the layout, still says current",
        AssetCook::isCookedCurrent(AssetType::Mesh, path)
    );

    // Put it back, so what follows tests what it means to.
    check("and the artifact rewrites", AssetCook::writeMesh(path, aSkinnedMesh()));

    // Not a cooked asset at all.
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const char junk[] = "this is not a cooked asset, it is a text file";
        out.write(junk, sizeof(junk));
    }
    MeshAsset fromJunk;
    check("a file that is not cooked at all is refused", !AssetCook::readMesh(path, fromJunk));
    check("  and is not current", !AssetCook::isCookedCurrent(AssetType::Mesh, path));

    std::error_code ec;
    std::filesystem::remove(path, ec);
    check("a file that is not there is not current", !AssetCook::isCookedCurrent(AssetType::Mesh, path));
    MeshAsset fromNothing;
    check("  and does not read", !AssetCook::readMesh(path, fromNothing));
}

// What a cooked artifact is filed under. A recipe names its source file, so a
// re-export leaves the recipe byte-identical: the key must fold in the source bytes,
// or the cook skips the work and editor and runtime serve the old bake, with no verb
// to force it.
void testTheCacheKeyMovesWhenTheSourceArtDoes() {
    std::printf("What a cook is addressed by:\n");

    const std::filesystem::path root =
        runScratch() / "vkm_cook_key";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "assets", ec);

    const std::filesystem::path previousRoot = ProjectPaths::projectRoot();
    ProjectPaths::setProjectRoot(root);

    const std::string ref = "assets/hero.glb";
    const auto write = [&](const std::string& bytes) {
        std::ofstream out(root / "assets" / "hero.glb", std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };

    write("the first export");
    const uint64_t first = AssetCooker::foldSourceContent(ref, 1234u);

    // Same seed, path and recipe - only the bytes moved, which a recipe-only key misses.
    write("the second export, re-exported from the modelling tool");
    const uint64_t second = AssetCooker::foldSourceContent(ref, 1234u);

    check("re-exporting the source art moves the key", first != second);
    check(
        "  and the same bytes give the same key again",
        [&] {
            write("the first export");
            return AssetCooker::foldSourceContent(ref, 1234u) == first;
        }()
    );

    // A generator or procedural source names no file and folds nothing, or each would
    // re-cook every pass.
    check("a recipe naming no file folds nothing", AssetCooker::foldSourceContent("", 1234u) == 1234u);

    // Art that is gone is not art that is there.
    std::filesystem::remove(root / "assets" / "hero.glb", ec);
    const uint64_t missing = AssetCooker::foldSourceContent(ref, 1234u);
    check("and missing art is not the same as present art", missing != first && missing != 1234u);

    // A real key as seed, as the cooker passes. The missing-art marker is a string, and
    // a seed read as its length would hash gigabytes past it.
    constexpr uint64_t REAL_KEY = 0x9E3779B97F4A7C15ull;
    check("  whatever the seed it is folded into", AssetCooker::foldSourceContent(ref, REAL_KEY) != REAL_KEY);

    ProjectPaths::setProjectRoot(previousRoot);
    std::filesystem::remove_all(root, ec);
}

// The loader only a player runs: a scene name to a cooked asset in the graph. A
// developer's machine has the recipes and never takes this path, so what it does when
// manifest and library disagree is what only a shipped game does.
void testTheCookedLoaderIsWhatAShippedGameReads() {
    std::printf("Loading an asset the way a shipped game does:\n");

    // A scratch project, so the manifest under test is not the examples'; torn down at
    // the end.
    const std::filesystem::path root = scratch("vkm_cooked_project");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "library", ec);

    const std::filesystem::path previousRoot = ProjectPaths::projectRoot();
    ProjectPaths::setProjectRoot(root);

    SkeletonAsset skeleton;
    skeleton.bones       = { Bone{"root", -1} };
    skeleton.inverseBind = { glm::mat4(1.0f) };
    skeleton.bindPose    = { Transform{} };

    constexpr uint64_t RECIPE = 0x5151u;
    AssetRecord record;
    record.type       = AssetType::Skeleton;
    record.name       = "rig:one";
    record.recipeHash = RECIPE;
    AssetLibrary::get().upsert(record);

    const std::filesystem::path cooked = AssetLibrary::cookedPath(AssetType::Skeleton, "rig:one", RECIPE);
    check("the cooker's artifact writes", AssetCook::writeSkeleton(cooked, skeleton));

    ResourceManager resources;
    const SkeletonHandle loaded = loadCookedSkeleton("rig:one", resources);
    check("the loader finds it by name", bool(loaded));
    check("  and it arrives whole", resources.isAlive(loaded) && resources.get(loaded).bones.size() == 1);
    check("  under the name the scene will ask for", resources.get(loaded).name() == "rig:one");

    // Asked twice: a resident name is the same asset, not a second read.
    const SkeletonHandle again = loadCookedSkeleton("rig:one", resources);
    check("asking again hands back the same asset", again == loaded);

    // A name the manifest never listed: a scene referencing an asset the cooker never saw.
    check("a name the manifest does not carry loads nothing", !loadCookedSkeleton("rig:missing", resources));

    // Listed, but not on disk - a package shipping its manifest without its library.
    AssetRecord ghost;
    ghost.type       = AssetType::Skeleton;
    ghost.name       = "rig:ghost";
    ghost.recipeHash = RECIPE;
    AssetLibrary::get().upsert(ghost);
    check(
        "a manifest entry with no file behind it loads nothing",
        !loadCookedSkeleton("rig:ghost", resources)
    );

    // Staleness is a path rule, not a comparison: the recipe is in the filename, so an
    // artifact from another recipe is never looked for, rather than rejected.
    AssetRecord moved = record;
    moved.recipeHash = RECIPE + 1;
    AssetLibrary::get().upsert(moved);
    ResourceManager fresh;
    check(
        "an artifact baked from another recipe is not the one loaded",
        !loadCookedSkeleton("rig:one", fresh)
    );

    AssetLibrary::get().remove(AssetType::Skeleton, "rig:one");
    AssetLibrary::get().remove(AssetType::Skeleton, "rig:ghost");
    ProjectPaths::setProjectRoot(previousRoot);
    std::filesystem::remove_all(root, ec);
}

// A runtime installs no recipe import, so the cooked file is the only way a scene's
// asset arrives; the load must reach it without one, and say why when it cannot.
void testAHostWithNoImportsLoadsWhatIsCooked() {
    std::printf("A scene load in a host that imports nothing:\n");

    const ScratchProject project("vkm_no_imports");
    const auto previousFactory = assetFactory().createSkeleton;
    assetFactory().createSkeleton = nullptr;

    SkeletonAsset skeleton;
    skeleton.bones       = { Bone{"root", -1} };
    skeleton.inverseBind = { glm::mat4(1.0f) };
    skeleton.bindPose    = { Transform{} };
    constexpr uint64_t RECIPE = 0x6262u;
    AssetLibrary::get().upsert({AssetType::Skeleton, "rig:cooked", RECIPE, {}});
    AssetLibrary::get().upsert({AssetType::Skeleton, "rig:uncooked", RECIPE, {}});
    const std::filesystem::path file = AssetLibrary::cookedPath(AssetType::Skeleton, "rig:cooked", RECIPE);
    check("the cooker's artifact writes", AssetCook::writeSkeleton(file, skeleton));

    ResourceManager resources;
    AssetSerializer::loadAssets(
        {{"skeletons", nlohmann::json::array({{{"name", "rig:cooked"}}, {{"name", "rig:uncooked"}}})}},
        resources
    );
    const SkeletonHandle cooked = resources.findByName<SkeletonAsset>("rig:cooked");
    check("the cooked asset loads", cooked && resources.get(cooked).bones.size() == 1);
    check("  and the one with no cooked file does not", !resources.findByName<SkeletonAsset>("rig:uncooked"));

    assetFactory().createSkeleton = previousFactory;
    AssetLibrary::get().remove(AssetType::Skeleton, "rig:cooked");
    AssetLibrary::get().remove(AssetType::Skeleton, "rig:uncooked");
}

// An asset with a recipe and nothing in it is a source that did not load; recording
// it would promise a cooked file nothing produced. That holds for every kind.
void testACookRefusesARecipeWithNothingInIt() {
    std::printf("Cooking an asset whose source did not load:\n");

    const ScratchProject project("vkm_cook_empty");
    const nlohmann::json recipe = {{"kind", AssetSourceKind::MODEL}, {"path", "gone.glb"}};
    const auto refused = [&](auto asset, const char* name) {
        asset.sourceJson() = recipe;
        ResourceManager resources;
        resources.add(std::move(asset), name);
        return !AssetCooker::cookAllAssets(resources);
    };
    check("an empty mesh fails the cook", refused(MeshAsset{}, "empty:mesh"));
    check("  so does an empty skeleton", refused(SkeletonAsset{}, "empty:rig"));
    check("  an empty clip", refused(AnimationClipAsset{}, "empty:clip"));
    check("  and an empty sound", refused(AudioClipAsset{}, "empty:sound"));
    check("  and none of them is recorded", !AssetLibrary::get().find(AssetType::Skeleton, "empty:rig"));
}

// The manifest is derived data, so a mistyped field means this build cannot read it:
// start the library empty and re-cook, rather than throw.
void testAManifestThatDoesNotReadStartsTheLibraryEmpty() {
    std::printf("A manifest with a field of the wrong type:\n");

    const ScratchProject project("vkm_manifest_mistyped");
    std::error_code ec;
    std::filesystem::create_directories(ProjectPaths::cooked(), ec);

    const auto loadFrom = [](const char* text) {
        {
            std::ofstream out(ProjectPaths::assetManifest(), std::ios::trunc);
            out << text;
        }
        bool threw = false;
        try {
            AssetLibrary::get().load(AssetLibrary::Truth::Recipes);
        } catch (...) {
            threw = true;
        }
        return !threw;
    };

    check(
        "a hash written as text does not throw",
        loadFrom(
            R"({"manifestVersion": 2, "assets": [)"
            R"({"type": "mesh", "name": "crate", "hash": "not a number"}]})"
        )
    );
    check("  and the library starts empty", !AssetLibrary::get().find(AssetType::Mesh, "crate"));

    check(
        "a source list written as text does not throw",
        loadFrom(
            R"({"manifestVersion": 2, "assets": [)"
            R"({"type": "mesh", "name": "crate", "hash": 7, "sources": "hero.bin"}]})"
        )
    );
    check("  and the library starts empty", !AssetLibrary::get().find(AssetType::Mesh, "crate"));

    check(
        "a version written as text does not throw either",
        loadFrom(R"({"manifestVersion": "one", "assets": []})")
    );
    check("nor a document that is not an object at all", loadFrom("[1, 2, 3]"));

    // A good one reads, so the refusals above are about the types and not the file.
    const nlohmann::json recipe = {{"kind", AssetSourceKind::GENERATOR}, {"type", "cube"}};
    check("a recipe for it is on disk", AssetLibrary::writeRecipe(AssetType::Mesh, "crate", recipe));
    const bool loaded = loadFrom(
        R"({"manifestVersion": 2, "assets": [)"
        R"({"type": "mesh", "name": "crate", "hash": 7, "sources": ["assets/crate.glb"]},)"
        R"({"type": "mesh", "name": "renamed", "hash": 9, "sources": []}]})"
    );
    const AssetRecord* crate = AssetLibrary::get().find(AssetType::Mesh, "crate");
    check(
        "a well-formed manifest still loads",
        loaded && crate && crate->sources == std::vector<std::string>{"assets/crate.glb"}
    );
    // Recipes are the truth: a row whose recipe is gone names an asset since renamed or
    // deleted.
    check("  less a row whose recipe is gone", !AssetLibrary::get().find(AssetType::Mesh, "renamed"));
    // A package ships no recipe but a material's.
    AssetLibrary::get().load(AssetLibrary::Truth::Manifest);
    check(
        "  which a host reading only cooked assets keeps",
        AssetLibrary::get().find(AssetType::Mesh, "renamed") != nullptr
    );
    AssetLibrary::get().remove(AssetType::Mesh, "renamed");
    AssetLibrary::get().remove(AssetType::Mesh, "crate");
}

// A derived asset's recipe names its source asset only by name, so the source changing
// leaves the recipe unchanged; the key must carry the source's key to rebake.
void testADerivedAssetsKeyMovesWithWhatItWasDerivedFrom() {
    std::printf("A level decimated from a mesh, and the mesh changing:\n");

    AssetRecord base;
    base.type       = AssetType::Mesh;
    base.name       = "crate:base";
    base.recipeHash = 0x1111u;
    AssetLibrary::get().upsert(base);
    const uint64_t first = AssetCooker::foldDependency(AssetType::Mesh, "crate:base", 99u);

    base.recipeHash = 0x2222u;
    AssetLibrary::get().upsert(base);
    const uint64_t second = AssetCooker::foldDependency(AssetType::Mesh, "crate:base", 99u);

    check("re-cooking the base moves the level's key", first != second && first != 99u);
    check(
        "a recipe naming nothing folds nothing",
        AssetCooker::foldDependency(AssetType::Mesh, "", 99u) == 99u
    );
    check(
        "and a base the manifest has never heard of is not a base it has",
        AssetCooker::foldDependency(AssetType::Mesh, "crate:gone", 99u) != 99u
            && AssetCooker::foldDependency(AssetType::Mesh, "crate:gone", 99u) != second
    );

    AssetLibrary::get().remove(AssetType::Mesh, "crate:base");
}

// A sphere with triangles and vertices in no useful order, each vertex bound by
// weights no other has - so a reorder losing a triangle or moving a binding shows.
MeshAsset aScrambledSkinnedSphere() {
    MeshAsset mesh = generateSphere();
    const uint32_t count = static_cast<uint32_t>(mesh.vertices.size());

    std::reverse(mesh.vertices.begin(), mesh.vertices.end());
    for (uint32_t& index : mesh.indices) index = count - 1 - index;

    std::vector<std::array<uint32_t, 3>> triangles;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        triangles.push_back({mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]});
    }
    std::shuffle(triangles.begin(), triangles.end(), std::mt19937(7));
    mesh.indices.clear();
    for (const auto& triangle : triangles) {
        mesh.indices.insert(mesh.indices.end(), triangle.begin(), triangle.end());
    }

    // Under MAX_SKELETON_BONES, which the cooked reader holds a bone index to.
    for (uint32_t i = 0; i < count; ++i) {
        mesh.skin.push_back(
            SkinVertex{
                {static_cast<uint16_t>(i % 1000), static_cast<uint16_t>(i / 1000), 0, 0},
                {200, 55, 0, 0}
            }
        );
    }
    mesh.skeleton = "rig:ball";
    return mesh;
}

// One corner as what it draws: the vertex's contents and its binding.
using Corner = std::array<float, 20>;

// Every triangle as its three corners, rotated to start at its least corner (keeping
// winding), then sorted: equal exactly when two meshes draw the same triangles bound
// the same way, in any order.
std::vector<std::array<Corner, 3>> drawnTriangles(const MeshAsset& mesh) {
    const auto cornerOf = [&](uint32_t index) {
        const Vertex&    v = mesh.vertices[index];
        const SkinVertex s = mesh.skin.empty() ? SkinVertex{} : mesh.skin[index];
        return Corner{
            v.position.x,
            v.position.y,
            v.position.z,
            v.normal.x,
            v.normal.y,
            v.normal.z,
            v.uv.x,
            v.uv.y,
            v.tangent.x,
            v.tangent.y,
            v.tangent.z,
            v.tangent.w,
            float(s.bones[0]),
            float(s.bones[1]),
            float(s.bones[2]),
            float(s.bones[3]),
            float(s.weights[0]),
            float(s.weights[1]),
            float(s.weights[2]),
            float(s.weights[3])
        };
    };

    std::vector<std::array<Corner, 3>> triangles;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        std::array<Corner, 3> t = {
            cornerOf(mesh.indices[i]),
            cornerOf(mesh.indices[i + 1]),
            cornerOf(mesh.indices[i + 2])
        };
        std::rotate(t.begin(), std::min_element(t.begin(), t.end()), t.end());
        triangles.push_back(t);
    }
    std::sort(triangles.begin(), triangles.end());
    return triangles;
}

// Vertices transformed per triangle through a 16-entry cache: 3 is every corner
// afresh; a good draw order lands well under 1.
float transformsPerTriangle(const MeshAsset& mesh) {
    return meshopt_analyzeVertexCache(
        mesh.indices.data(),
        mesh.indices.size(),
        mesh.vertices.size(),
        16,
        0,
        0
    ).acmr;
}

void testReorderingKeepsEveryTriangleAndBinding() {
    std::printf("A mesh reordered for the GPU:\n");

    const MeshAsset scrambled = aScrambledSkinnedSphere();
    MeshAsset reordered(scrambled);
    optimizeMeshForGpu(reordered);

    check(
        "it draws the same triangles, wound the same way, each corner bound as it was",
        reordered.skin.size() == reordered.vertices.size()
            && drawnTriangles(reordered) == drawnTriangles(scrambled)
    );
    check(
        "  in an order the vertex cache serves",
        transformsPerTriangle(reordered) < transformsPerTriangle(scrambled) * 0.5f
    );
    check(
        "  with its vertices in the order its triangles first use them",
        !reordered.indices.empty() && reordered.indices[0] == 0
    );
    check(
        "its bounds and rig are untouched",
        reordered.boundsMin == scrambled.boundsMin
            && reordered.boundsMax == scrambled.boundsMax
            && reordered.skeleton == scrambled.skeleton
    );

    MeshAsset malformed(scrambled);
    malformed.indices.push_back(0);
    const std::vector<uint32_t> before = malformed.indices;
    optimizeMeshForGpu(malformed);
    check("a partial triangle is left as it came rather than read past", malformed.indices == before);
}

// A shipped game draws what the cook wrote, so the cooked file, not the live asset,
// must be in draw order.
void testTheCookBakesAMeshInDrawOrder() {
    std::printf("A mesh baked through the cooker:\n");

    const ScratchProject project("vkm_cook_draw_order");

    ResourceManager resources;
    MeshAsset live = aScrambledSkinnedSphere();
    live.sourceJson() = {{"kind", AssetSourceKind::GENERATOR}, {"type", "sphere"}};
    const MeshHandle handle = resources.add(std::move(live), "ball");
    check("it cooks", AssetCooker::cookAllAssets(resources));

    const AssetRecord* record = AssetLibrary::get().find(AssetType::Mesh, "ball");
    MeshAsset baked;
    check(
        "  and the cooked file reads",
        record
            && AssetCook::readMesh(
                AssetLibrary::cookedPath(AssetType::Mesh, "ball", record->recipeHash),
                baked
            )
    );

    const MeshAsset& source = resources.get(handle);
    check(
        "the file draws every triangle the asset does, bound as it was",
        !baked.indices.empty() && drawnTriangles(baked) == drawnTriangles(source)
    );
    check(
        "  in draw order",
        !baked.indices.empty() && transformsPerTriangle(baked) < transformsPerTriangle(source) * 0.5f
    );
    check(
        "while the live asset keeps the order it was given",
        source.indices == aScrambledSkinnedSphere().indices
    );

    AssetLibrary::get().remove(AssetType::Mesh, "ball");
}

// A .gltf keeps its vertices in a .bin beside it, so a re-export can leave the .gltf
// byte-identical. The key must read the buffer too, or the old mesh is served for ever -
// and the record must say it read it, or a package ships it as if the game did.
void testEveryFileAModelReadsIsInItsKey() {
    std::printf("What a cook of a .gltf reads:\n");

    const ScratchProject project("vkm_cook_gltf");
    std::error_code ec;
    std::filesystem::create_directories(project.root() / "assets/hero parts", ec);
    const auto write = [&](const char* file, const std::string& bytes) {
        std::ofstream out(project.root() / "assets" / file, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    const char* gltf = R"({"asset": {"version": "2.0"}, "buffers": [
        {"uri": "hero%20parts/hero.bin", "byteLength": 12},
        {"uri": "data:application/octet-stream;base64,AAAA", "byteLength": 3}]})";
    write("hero.gltf", gltf);
    write("hero parts/hero.bin", "first export");

    const std::vector<std::string> files = AssetCooker::sourceFiles("assets/hero.gltf");
    check(
        "a .gltf reads itself and the buffer it names, unescaped, and no data: URI",
        files == std::vector<std::string>{"assets/hero.gltf", "assets/hero parts/hero.bin"}
    );
    check(
        "  while any other source reads itself alone",
        AssetCooker::sourceFiles("assets/hero.glb") == std::vector<std::string>{"assets/hero.glb"}
    );

    write("crate.obj", "# a crate\nmtllib crate paint.mtl\nv 0 0 0\n");
    write("crate.mtl", "newmtl wood\n");
    check(
        "an .obj reads the library it names, spaces and all, and the one named after it",
        AssetCooker::sourceFiles("assets/crate.obj")
            == std::vector<std::string>{"assets/crate.obj", "assets/crate paint.mtl", "assets/crate.mtl"}
    );

    // Each graph is an import of the file as on disk then: a cook keys its asset by the
    // bytes it was made from.
    const auto importHero = [](ResourceManager& resources) {
        MeshAsset mesh = aSkinnedMesh();
        mesh.skin.clear();
        mesh.skeleton.clear();
        mesh.sourceJson() = {{"kind", AssetSourceKind::MODEL}, {"path", "assets/hero.gltf"}, {"mesh", 0}};
        resources.add(std::move(mesh), "hero:mesh0");
    };
    ResourceManager firstImport;
    importHero(firstImport);
    check("the model cooks", AssetCooker::cookAllAssets(firstImport));
    const AssetRecord* record = AssetLibrary::get().find(AssetType::Mesh, "hero:mesh0");
    const uint64_t first = record ? record->recipeHash : 0;
    check("  and its record lists the files it read", record && record->sources == files);

    write("hero parts/hero.bin", "second export with new normals");
    ResourceManager secondImport;
    importHero(secondImport);
    check("re-exporting the buffer alone re-cooks it", AssetCooker::cookAllAssets(secondImport));
    record = AssetLibrary::get().find(AssetType::Mesh, "hero:mesh0");
    check("  under a key that moved with it", record && record->recipeHash != first);

    AssetLibrary::get().remove(AssetType::Mesh, "hero:mesh0");
}

// The editor cooks on every Save and Play, and a key reads every art file a recipe
// names - a project's largest data. A cook bakes the asset in memory, which disk changes
// do not change, so art is read the first time a session cooks an asset, not every save.
void testASaveDoesNotReadTheArtAgain() {
    std::printf("A second save of an asset the session already cooked:\n");

    const ScratchProject project("vkm_cook_session_key");
    std::error_code ec;
    std::filesystem::create_directories(project.root() / "assets", ec);
    std::ofstream(project.root() / "assets" / "art.png", std::ios::binary) << "the art as imported";

    ResourceManager editor;
    TextureAsset art = aDecodedTexture(4, 4, TextureInternalFormat::RGBA8);
    art.sourceJson() = {
        {"kind", AssetSourceKind::FILE},
        {"path", "assets/art.png"},
        {"usage", "Data"},
        {"generateMipmaps", false}
    };
    editor.add(std::move(art), "tex:art");
    check("the first save cooks it", AssetCooker::cookAllAssets(editor));
    const AssetRecord* record = AssetLibrary::get().find(AssetType::Texture, "tex:art");
    const uint64_t first = record ? record->recipeHash : 0;

    // Gone from disk: a save reading it again would key it as missing art and re-cook.
    std::filesystem::remove(project.root() / "assets" / "art.png", ec);
    check("a second save succeeds", AssetCooker::cookAllAssets(editor));
    record = AssetLibrary::get().find(AssetType::Texture, "tex:art");
    check(
        "  without reading the art: the asset is filed where it was",
        record && record->recipeHash == first
    );

    AssetLibrary::get().remove(AssetType::Texture, "tex:art");
}

// A cook deletes the file the previous record named, but an older cooker's output and a
// killed cook's temporary are named by no record. Never read again, they would ship in
// every package; the explicit cook deletes them.
void testTheExplicitCookLeavesOnlyWhatARecordNames() {
    std::printf("Cooked files no record names:\n");

    const ScratchProject project("vkm_cook_orphans");
    ResourceManager resources;
    TextureAsset kept = aDecodedTexture(4, 4, TextureInternalFormat::RGBA8);
    kept.sourceJson() = {
        {"kind", AssetSourceKind::FILE},
        {"path", "assets/kept.png"},
        {"usage", "Data"},
        {"generateMipmaps", false}
    };
    resources.add(std::move(kept), "tex:kept");
    check("a texture cooks", AssetCooker::cookAllAssets(resources));
    const AssetRecord* record = AssetLibrary::get().find(AssetType::Texture, "tex:kept");
    const std::filesystem::path artifact = record
        ? AssetLibrary::cookedPath(AssetType::Texture, "tex:kept", record->recipeHash)
        : std::filesystem::path{};

    const std::filesystem::path olderCooker = artifact.parent_path()
        / "0123456789abcdef-feedface00000000.vkmc";
    const std::filesystem::path interrupted = std::filesystem::path(artifact).concat(".tmp");
    std::ofstream(olderCooker) << "baked by a cooker this build is not";
    std::ofstream(interrupted) << "half a bake";

    check("the two strays are removed", AssetLibrary::get().removeUnrecordedCooked() == 2);
    std::error_code ec;
    check(
        "  leaving the file the record names",
        !artifact.empty()
            && std::filesystem::exists(artifact, ec)
            && !std::filesystem::exists(olderCooker, ec)
            && !std::filesystem::exists(interrupted, ec)
    );

    AssetLibrary::get().remove(AssetType::Texture, "tex:kept");
}

// The inspector regenerates a mesh's levels on every press, replacing the last press's
// levels under their existing handles rather than adding a set beside them.
void testRegeneratingLevelsReplacesThem() {
    std::printf("Generating a mesh's levels a second time:\n");

    ResourceManager resources;
    const MeshHandle base = resources.add(generateSphere(32, 16), "lod:base");
    const LOD first = generateLOD(resources, base, 2);
    const size_t meshes = resources.countOfType<MeshAsset>();
    std::vector<uint64_t> versions;
    for (const LODLevel& level : first.levels) versions.push_back(resources.get(level.mesh).version());

    const LOD second = generateLOD(resources, base, 2);
    bool sameHandles = first.levels.size() == second.levels.size() && first.levels.size() > 1;
    bool rebuilt     = sameHandles;
    for (size_t i = 1; sameHandles && i < second.levels.size(); ++i) {
        sameHandles = second.levels[i].mesh == first.levels[i].mesh;
        rebuilt     = rebuilt && resources.get(second.levels[i].mesh).version() > versions[i];
    }
    check("the levels keep their handles", sameHandles);
    check("  holding what the second press made", rebuilt);
    check("  and no asset is added beside them", resources.countOfType<MeshAsset>() == meshes);
}

// Stands in for the recipe dispatch the suite cannot link: one bone per byte of the
// recipe's file, so a re-export visibly changes the bake.
SkeletonHandle importOneBonePerByte(const nlohmann::json& source, ResourceManager& resources) {
    std::ifstream in(ProjectPaths::resolveProjectPath(source.value("path", std::string{})), std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (bytes.empty()) return {};

    SkeletonAsset rig;
    for (size_t i = 0; i < bytes.size(); ++i) {
        rig.bones.push_back(Bone{"bone" + std::to_string(i), static_cast<int>(i) - 1});
        rig.inverseBind.push_back(glm::mat4(1.0f));
        rig.bindPose.push_back(Transform{});
    }
    rig.sourceJson() = source;
    return resources.add(std::move(rig), "rig:imported");
}

// The cooked cache serves an asset by its manifest row's key, and such an asset carries
// no recipe to re-hash, so art re-exported after the first bake is invisible to loads
// and saves; only the explicit cook, re-hashing what the manifest names, sees it.
void testTheExplicitCookRebakesWhatChangedUnderTheCache() {
    std::printf("Re-exported art behind an asset the cache already serves:\n");

    const ScratchProject project("vkm_cook_stale");
    std::error_code ec;
    std::filesystem::create_directories(project.root() / "assets", ec);
    const auto exportArt = [&](const std::string& bytes) {
        std::ofstream out(project.root() / "assets" / "rig.bin", std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };

    const auto previousFactory = assetFactory().createSkeleton;
    assetFactory().createSkeleton = &importOneBonePerByte;

    const std::string name = "rig:hero";
    const nlohmann::json recipe = {{"kind", AssetSourceKind::MODEL}, {"path", "assets/rig.bin"}};
    const auto bonesServed = [&] {
        ResourceManager player;
        const SkeletonHandle rig = loadCookedSkeleton(name, player);
        return rig ? player.get(rig).bones.size() : size_t{0};
    };

    exportArt("ab");
    {
        ResourceManager editor;
        editor.rename(importOneBonePerByte(recipe, editor), name);
        check("the first bake cooks", AssetCooker::cookAllAssets(editor));
    }
    const AssetRecord* record = AssetLibrary::get().find(AssetType::Skeleton, name);
    const uint64_t firstKey = record ? record->recipeHash : 0;
    check("  and the cache serves it", bonesServed() == 2);

    check("a sweep over nothing that changed succeeds", AssetCooker::cookStaleAssets());
    record = AssetLibrary::get().find(AssetType::Skeleton, name);
    check("  and leaves the record where it was", record && record->recipeHash == firstKey);

    exportArt("abcd");
    check("the art moved and the cache still serves the old bake", bonesServed() == 2);
    check("the explicit cook succeeds", AssetCooker::cookStaleAssets());
    record = AssetLibrary::get().find(AssetType::Skeleton, name);
    check("  records a new key", record && record->recipeHash != firstKey);
    check("  and the cache now serves the new art", bonesServed() == 4);
    check(
        "  and the file the old key named is gone, not left to pile up",
        !std::filesystem::exists(AssetLibrary::cookedPath(AssetType::Skeleton, name, firstKey), ec)
    );

    // A key also stops finding its file when a cooker or format version bump renames
    // every file; a deleted cache stands in.
    std::filesystem::remove_all(ProjectPaths::cooked(), ec);
    check("a cache that is gone is baked again", AssetCooker::cookStaleAssets() && bonesServed() == 4);

    assetFactory().createSkeleton = previousFactory;
    AssetLibrary::get().remove(AssetType::Skeleton, name);
}

// Recipes are not derived; the manifest is. A recipe's path comes from the asset name
// alone, so a deleted or version-refused manifest costs a re-cook, not every asset.
void testANameTheManifestLostStillLoadsFromItsRecipe() {
    std::printf("An asset the manifest has no row for:\n");

    const ScratchProject project("vkm_manifest_lost");
    const nlohmann::json recipe = {{"kind", AssetSourceKind::INLINE}, {"roughness", 0.25f}};
    check("its recipe is on disk", AssetLibrary::writeRecipe(AssetType::Material, "mat:kept", recipe));
    check("  and the manifest has no row for it", !AssetLibrary::get().find(AssetType::Material, "mat:kept"));

    ResourceManager resources;
    AssetSerializer::loadAssets(
        {{"materials", nlohmann::json::array({{{"name", "mat:kept"}}, {{"name", "mat:gone"}}})}},
        resources
    );
    const MaterialHandle kept = resources.findByName<MaterialAsset>("mat:kept");
    check("it loads from the recipe", bool(kept));

    // A fresh clone: recipes committed, cooked/ and its manifest never made.
    AssetLibrary::get().load(AssetLibrary::Truth::Recipes);
    const AssetRecord* adopted = AssetLibrary::get().find(AssetType::Material, "mat:kept");
    check("  and a load gives it a row, with no cook recorded", adopted && adopted->recipeHash == 0);
    check("  so the pickers list it", !AssetLibrary::get().namesOf(AssetType::Material).empty());
    AssetLibrary::get().remove(AssetType::Material, "mat:kept");
    check("  holding what the recipe says", kept && nearly(resources.get(kept).roughness, 0.25f));
    check(
        "a name with no recipe either still loads nothing",
        !resources.findByName<MaterialAsset>("mat:gone")
    );
}

// A recipe is hand-editable, and a mistyped value throws out of its reader; out of a
// scene load that would lose the whole scene and name no asset. It must cost one
// asset, by name.
void testAMistypedRecipeCostsItsAssetNotTheScene() {
    std::printf("A recipe holding a value of the wrong type:\n");

    const ScratchProject project("vkm_recipe_mistyped");
    const nlohmann::json fine = {{"kind", AssetSourceKind::INLINE}, {"roughness", 0.25f}};
    const nlohmann::json typo = {{"kind", AssetSourceKind::INLINE}, {"metallic", "0.5"}};
    check(
        "the recipes are on disk",
        AssetLibrary::writeRecipe(AssetType::Material, "mat:fine", fine)
            && AssetLibrary::writeRecipe(AssetType::Material, "mat:typo", typo)
    );

    const std::filesystem::path scenePath = project.root() / "mistyped.json";
    std::ofstream(scenePath) << nlohmann::json{
        {"version", 1},
        {"entities", nlohmann::json::array()},
        {"assets", {{"materials", nlohmann::json::array({{{"name", "mat:typo"}}, {{"name", "mat:fine"}}})}}},
    }.dump();

    EngineErrorLog errors;
    setErrorSink(&errors);
    Scene scene;
    ResourceManager resources;
    const bool loaded = SceneSerializer::load(scene, resources, scenePath.string());
    setErrorSink(nullptr);

    check("the scene still loads", loaded);
    check("  with the asset beside it", bool(resources.findByName<MaterialAsset>("mat:fine")));
    check("  and without the one that does not read", !resources.findByName<MaterialAsset>("mat:typo"));
    const auto namesTheRecipe = [](const EngineErrorLog::Entry& e) {
        return e.source.find("mat:typo") != std::string::npos && e.message.find(".json") != std::string::npos;
    };
    const bool named = std::any_of(errors.entries().begin(), errors.entries().end(), namesTheRecipe);
    check("  reported by name, with the recipe file to fix", named);

    // A .gltf's named files are read to key its cook by the same rule: a non-string
    // buffer uri is not a buffer, and not a throw.
    std::ofstream(project.root() / "odd.gltf")
        << R"({"asset": {"version": "2.0"}, "buffers": [{"uri": 7}, 3]})";
    check(
        "a .gltf whose buffer uri is not a string reads as itself alone",
        AssetCooker::sourceFiles("odd.gltf") == std::vector<std::string>{"odd.gltf"}
    );
}

// An importer deduping by its own identity (a texture by path), answering a second
// library entry for the same file with the first's asset.
TextureHandle importTheOneAlreadyLoaded(const nlohmann::json&, ResourceManager& resources) {
    return resources.findByName<TextureAsset>("art.png");
}

// A load files what a factory returns under the requested name. If that is an asset
// the graph already holds under its own name, renaming would take that name from it -
// and every material and scene resolving it - for an entry never loaded.
void testALoadNeverRenamesAnAssetThatWasAlreadyThere() {
    std::printf("Two library entries that import the same file:\n");

    const ScratchProject project("vkm_load_rename");
    const auto previousFactory = assetFactory().createTexture;
    assetFactory().createTexture = &importTheOneAlreadyLoaded;

    const nlohmann::json recipe = {{"kind", AssetSourceKind::FILE}, {"path", "art.png"}};
    check(
        "the second entry's recipe writes",
        AssetLibrary::writeRecipe(AssetType::Texture, "hero:albedo", recipe)
    );

    ResourceManager resources;
    TextureAsset art;
    art.pixelData = {1, 2, 3, 4};
    const TextureHandle loaded = resources.add(std::move(art), "art.png");

    AssetSerializer::loadAssets(
        {{"textures", nlohmann::json::array({{{"name", "hero:albedo"}}})}},
        resources
    );

    check(
        "the asset that was there keeps its name",
        resources.findByName<TextureAsset>("art.png") == loaded && resources.get(loaded).name() == "art.png"
    );
    check(
        "  and the entry it cannot be is left unresolved rather than stolen",
        !resources.findByName<TextureAsset>("hero:albedo")
    );

    assetFactory().createTexture = previousFactory;
}

// What the cooker at this COOKER_VERSION makes of the fixed inputs below. The version
// is in every artifact's name, so a bump strands old artifacts and the next cook bakes
// new ones; changing the bytes without a bump leaves every project serving the old
// output under a name claiming otherwise.
constexpr uint32_t PINNED_COOKER_VERSION = 12;
constexpr uint64_t PINNED_COOKER_OUTPUT  = 0x5bf5388222df3440ull;

// Everything a cook decides itself, from fixed inputs: a scrambled mesh (vertex
// orderer), two maps through BC4/BC5, a cut-out colour map through the mip filter,
// coverage scale and BC7, and a model through the import's weld and tangents. Integer
// inputs and float code rounding alike in every build type (no FMA on the x86-64
// baseline, no reassociation without -ffast-math), so the bytes match in every build.
uint64_t hashOfAFixedCook() {
    MeshAsset mesh;
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            mesh.vertices.push_back(
                Vertex{
                    {static_cast<float>(x), 0.0f, static_cast<float>(z)},
                    {0, 1, 0},
                    {x * 0.25f, z * 0.25f},
                    {1, 0, 0, 1}
                }
            );
        }
    }
    for (int cell = 8; cell >= 0; --cell) {
        const uint32_t corner = static_cast<uint32_t>(cell / 3 * 4 + cell % 3);
        mesh.indices.insert(
            mesh.indices.end(),
            {corner, corner + 4, corner + 1, corner + 1, corner + 4, corner + 5}
        );
    }
    optimizeMeshForGpu(mesh);
    uint64_t hash = fnv1a64Bytes(mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
    hash = fnv1a64Bytes(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t), hash);

    const auto bake = [&hash](const TextureAsset& source) {
        TextureAsset baked;
        if (!AssetCooker::bakeTexture(source, baked)) return false;
        hash = fnv1a64Bytes(&baked.params.internalFormat, sizeof(baked.params.internalFormat), hash);
        hash = fnv1a64Bytes(&baked.params.mipLevels, sizeof(baked.params.mipLevels), hash);
        hash = fnv1a64Bytes(baked.pixelData.data(), baked.pixelData.size(), hash);
        return true;
    };
    for (const TextureInternalFormat format : {TextureInternalFormat::R8, TextureInternalFormat::RG8}) {
        TextureAsset source = aDecodedTexture(8, 8, format);
        source.params.generateMipmaps = false;
        for (size_t i = 0; i < source.pixelData.size(); ++i) {
            source.pixelData[i] = static_cast<uint8_t>((i * i * 7 + i * 13) & 0xFF);
        }
        if (!bake(source)) return 0;
    }

    // A cut-out: 2x2 cells of alpha either side of the cutoff, one in three opaque,
    // which filtering averages to or under the cutoff and the coverage scale must pull
    // back apart, level by level, to 1x1.
    TextureAsset cutOut = aDecodedTexture(16, 16, TextureInternalFormat::SRGBA8);
    cutOut.params.generateMipmaps = true;
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            uint8_t* texel = &cutOut.pixelData[(y * 16 + x) * 4];
            texel[0] = static_cast<uint8_t>(x * 16);
            texel[1] = static_cast<uint8_t>(y * 16);
            texel[2] = static_cast<uint8_t>((x + y) * 8);
            texel[3] = ((x / 2 + y / 2) % 3 == 0) ? 255 : 0;
        }
    }
    if (!bake(cutOut)) return 0;

    // The import half: an OBJ has no tangents, so MikkTSpace builds them; its corners
    // repeat, so the weld folds them.
    const std::filesystem::path obj = scratch("vkm_cook_pin.obj");
    const char* objText = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\nv 2 1 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvt 2 0\nvt 2 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n"
        "f 2/2/1 5/5/1 6/6/1\nf 2/2/1 6/6/1 3/3/1\n";
    std::ofstream(obj) << objText;
    const std::unique_ptr<SourceModel> model = parseModel(obj.string(), "vkm_cook_pin.obj");
    std::error_code ec;
    std::filesystem::remove(obj, ec);
    if (!model || model->meshes.size() != 1) return 0;
    const MeshAsset& imported = model->meshes[0].geometry;
    hash = fnv1a64Bytes(imported.vertices.data(), imported.vertices.size() * sizeof(Vertex), hash);
    hash = fnv1a64Bytes(imported.indices.data(), imported.indices.size() * sizeof(uint32_t), hash);
    return hash;
}

void testTheCookersOutputIsPinnedToItsVersion() {
    std::printf("What this cooker version bakes:\n");

    const uint64_t output = hashOfAFixedCook();
    std::printf(
        "      COOKER_VERSION %u bakes %016llx\n",
        AssetCook::COOKER_VERSION,
        static_cast<unsigned long long>(output)
    );
    if (AssetCook::COOKER_VERSION != PINNED_COOKER_VERSION) {
        check("COOKER_VERSION moved: pin the pair printed above in this file", false);
        return;
    }
    check(
        "the bytes are the ones this version has always baked - if the cooker's output "
        "changed on purpose, bump COOKER_VERSION and pin the new pair",
        output == PINNED_COOKER_OUTPUT
    );
}

// LOD levels are drawn in the mesh's place at range, unchecked: a bad level is
// geometry turning to soup in the distance, mistaken for a modelling error.
void testGeneratingLevelsOfDetail() {
    std::printf("Levels built below an imported mesh:\n");

    ResourceManager resources;
    const MeshHandle source = resources.add(generateSphere(), "lod:sphere");
    const size_t sourceTris = resources.get(source).indices.size() / 3;

    const LOD lod = generateLOD(resources, source, 2);
    check("the source is level 0", !lod.levels.empty() && lod.levels[0].mesh == source);
    check("  and levels were built below it", lod.levels.size() > 1);

    // Each level coarser than the last and a registered asset - an anonymous mesh would
    // not survive a scene save.
    bool coarsens = true;
    bool named    = true;
    for (size_t i = 1; i < lod.levels.size(); ++i) {
        const MeshAsset* mesh = resources.tryGet(lod.levels[i].mesh);
        if (!mesh) {
            named = false;
            break;
        }
        named = named && !mesh->name().empty()
            && resources.findByName<MeshAsset>(mesh->name()) == lod.levels[i].mesh;
        const size_t tris = mesh->indices.size() / 3;
        const size_t above = i == 1 ? sourceTris : resources.get(lod.levels[i - 1].mesh).indices.size() / 3;
        coarsens = coarsens && tris > 0 && tris < above;
    }
    check("  each level is coarser than the one above it", coarsens);
    check("  and each is a named asset a scene save can keep", named);

    // Switch distances must grow with level, or the coarsest would draw up close.
    bool ordered = true;
    for (size_t i = 1; i < lod.levels.size(); ++i) {
        ordered = ordered && lod.levels[i].maxDistance > lod.levels[i - 1].maxDistance;
    }
    check("  with switch distances that grow outward", ordered);

    // A triangle has no simpler form, and a no-op level costs a comparison every frame.
    MeshAsset flat = generateTriangle();
    const MeshHandle tiny = resources.add(std::move(flat), "lod:triangle");
    const LOD none = generateLOD(resources, tiny, 3);
    check("a mesh with nothing to lose gains no levels", none.levels.size() <= 1);

    // An unresolvable source is a component with no levels rather than a crash.
    check("an unresolvable source yields nothing", generateLOD(resources, MeshHandle{}, 2).levels.empty());

    // A mesh decimation refuses comes back empty, not copied: a copy would carry the
    // source's name, and the recipe load adding it before renaming would replace the base.
    MeshAsset skinned = generateCube();
    skinned.skin.assign(skinned.vertices.size(), SkinVertex{{0, 0, 0, 0}, {255, 0, 0, 0}});
    const MeshHandle rigged = resources.add(std::move(skinned), "lod:rigged");
    const MeshAsset refused = decimateMesh(resources.get(rigged), 0.5f);
    check(
        "a skinned mesh is not decimated, and says so by coming back empty",
        refused.vertices.empty() && refused.name().empty()
    );
    check(
        "a mesh with nothing to remove also comes back empty",
        decimateMesh(generateTriangle(), 0.5f).vertices.empty()
    );
    check(
        "  as does a ratio that asks for no reduction, or for all of one",
        decimateMesh(resources.get(source), 1.0f).vertices.empty()
            && decimateMesh(resources.get(source), 0.0f).vertices.empty()
            && decimateMesh(resources.get(source), std::numeric_limits<float>::quiet_NaN()).vertices.empty()
    );
    const MeshAsset half = decimateMesh(resources.get(source), 0.5f);
    check(
        "  and a decimated level carries no name of its own",
        !half.vertices.empty() && half.name().empty()
    );
}

// A level is worth drawing where its error stops showing, which depends on how far its
// surface moved - the mesh's size, not one distance for all. A ten-metre rock and a
// pebble decimated alike hand over ten times as far apart.
void testALevelHandsOverWhereItsErrorStopsShowing() {
    std::printf("Where one level hands over to the next:\n");

    ResourceManager resources;
    MeshAsset pebble = generateSphere(48, 24);
    MeshAsset rock   = pebble;
    for (Vertex& vertex : rock.vertices) vertex.position *= 10.0f;
    rock.computeAndSetBounds();
    const MeshHandle pebbleHandle = resources.add(std::move(pebble), "lod:pebble");
    const MeshHandle rockHandle   = resources.add(std::move(rock), "lod:rock");

    const LOD small = generateLOD(resources, pebbleHandle, 2);
    const LOD large = generateLOD(resources, rockHandle, 2);
    check("both build their levels", small.levels.size() > 1 && small.levels.size() == large.levels.size());
    for (size_t i = 0; i < small.levels.size(); ++i) {
        std::printf(
            "      level %zu: the pebble's to %.2f, the rock's to %.2f\n",
            i,
            static_cast<double>(small.levels[i].maxDistance),
            i < large.levels.size() ? static_cast<double>(large.levels[i].maxDistance) : 0.0
        );
    }

    float error = 0.0f;
    const MeshAsset first = decimateMesh(resources.get(pebbleHandle), 0.5f, &error);
    check("decimation reports the error it left", !first.vertices.empty() && error > 0.0f);

    // A pixel of a 1080-line image at 60 degrees is 2 tan(30) / 1080 of the distance,
    // so the hand-over is the error times 1080 / (2 tan 30).
    const float expected = error * 1080.0f / (2.0f * std::tan(glm::radians(30.0f)));
    check(
        "  and the source hands over where that error is a pixel",
        small.levels.size() > 1 && std::abs(small.levels[0].maxDistance - expected) <= expected * 0.01f
    );

    bool scales = small.levels.size() == large.levels.size();
    for (size_t i = 0; scales && i < small.levels.size(); ++i) {
        scales = std::abs(large.levels[i].maxDistance - 10.0f * small.levels[i].maxDistance)
            <= small.levels[i].maxDistance * 0.1f;
    }
    check("  so a mesh ten times the size hands over ten times as far", scales);
}

// A level replaces its source at range, so it may not change shape: grow past the
// source, shrink from it, or move a vertex.
void testDecimationKeepsTheShape() {
    std::printf("A level simplified from its source:\n");

    const MeshAsset source = generateSphere();
    const MeshAsset level  = decimateMesh(source, 0.5f);
    const size_t sourceTris = source.indices.size() / 3;
    const size_t levelTris  = level.indices.size() / 3;

    check("it has fewer triangles", levelTris > 0 && levelTris < sourceTris);
    check("  about as many as it was asked for", levelTris <= sourceTris * 6 / 10);

    bool indexed = !level.indices.empty();
    for (const uint32_t index : level.indices) indexed = indexed && index < level.vertices.size();
    check("  every index names a vertex the level holds", indexed);
    check("  and it holds only the vertices it uses", level.vertices.size() < source.vertices.size());

    bool unmoved = !level.vertices.empty();
    for (const Vertex& kept : level.vertices) {
        bool found = false;
        for (const Vertex& original : source.vertices) {
            if (kept.position == original.position && kept.normal == original.normal
                && kept.uv == original.uv) {
                found = true;
                break;
            }
        }
        unmoved = unmoved && found;
    }
    check("every vertex it keeps is one of the source's, unmoved", unmoved);

    const glm::vec3 sourceExtent = source.boundsMax - source.boundsMin;
    const glm::vec3 levelExtent  = level.boundsMax - level.boundsMin;
    check(
        "its bounds sit inside the source's",
        glm::all(glm::greaterThanEqual(level.boundsMin, source.boundsMin))
            && glm::all(glm::lessThanEqual(level.boundsMax, source.boundsMax))
    );
    check("  and span nearly all of them", glm::all(glm::greaterThanEqual(levelExtent, sourceExtent * 0.9f)));
}

// The decimate recipe reads back what it writes, and a recipe that does not say - no
// ratio, an unknown `grid`, a non-numeric ratio - gets the default rather than failing.
void testTheDecimateRecipeReadsWhatItWrites() {
    std::printf("The recipe a decimated level is rebuilt from:\n");

    const nlohmann::json recipe = decimateRecipe("crate", 0.125f);
    check("it names its base", recipe.value(AssetSourceKey::BASE, std::string{}) == "crate");
    check("  and the ratio comes back as written", nearly(decimateRatioFromRecipe(recipe), 0.125f));

    const nlohmann::json bare = {{"kind", AssetSourceKind::DECIMATE}, {AssetSourceKey::BASE, "crate"}};
    const float fallback = decimateRatioFromRecipe(bare);
    check("a recipe with no ratio asks for a real reduction", fallback > 0.0f && fallback < 1.0f);

    nlohmann::json clustered = bare;
    clustered["grid"] = 12;
    check(
        "  and so does one naming only a clustering grid",
        nearly(decimateRatioFromRecipe(clustered), fallback)
    );

    nlohmann::json mistyped = bare;
    mistyped[AssetSourceKey::RATIO] = "half";
    check(
        "a ratio that is not a number is the default, not an exception",
        nearly(decimateRatioFromRecipe(mistyped), fallback)
    );
}

} // namespace

void runCookTests() {
    testAMeshSurvivesTheRoundTrip();
    testAnUnskinnedMeshCarriesNoSkin();
    testASkeletonAndAClipSurviveTheRoundTrip();
    testATextureAndASoundSurviveTheRoundTrip();
    testAMipChainHasALevelPerHalving();
    testACompressedTextureSurvivesTheRoundTrip();
    testTheBlocksHoldTheTexelsTheyWereMadeFrom();
    testTheBC4EncoderFitsItsBlocks();
    testAnSrgbTextureIsFilteredInLinearLight();
    testAChainIsRoundedOncePerLevel();
    testANormalMapsLevelsAreDirections();
    testARoughnessMapTakesItsNormalMapsLostDetail();
    testACutOutKeepsItsCoverageAtRange();
    testTheCookWritesTheBakedTexture();
    testASaveRecordsNowAndBakesInTheBackground();
    testTheReaderRefusesLevelsItsBytesDoNotHold();
    testADamagedFileIsRefusedRatherThanRead();
    testTheCookedLoaderIsWhatAShippedGameReads();
    testAHostWithNoImportsLoadsWhatIsCooked();
    testACookRefusesARecipeWithNothingInIt();
    testTheCacheKeyMovesWhenTheSourceArtDoes();
    testEveryFileAModelReadsIsInItsKey();
    testASaveDoesNotReadTheArtAgain();
    testTheExplicitCookLeavesOnlyWhatARecordNames();
    testRegeneratingLevelsReplacesThem();
    testADerivedAssetsKeyMovesWithWhatItWasDerivedFrom();
    testReorderingKeepsEveryTriangleAndBinding();
    testTheCookBakesAMeshInDrawOrder();
    testTheCookersOutputIsPinnedToItsVersion();
    testAManifestThatDoesNotReadStartsTheLibraryEmpty();
    testTheExplicitCookRebakesWhatChangedUnderTheCache();
    testANameTheManifestLostStillLoadsFromItsRecipe();
    testAMistypedRecipeCostsItsAssetNotTheScene();
    testALoadNeverRenamesAnAssetThatWasAlreadyThere();
    testGeneratingLevelsOfDetail();
    testALevelHandsOverWhereItsErrorStopsShowing();
    testDecimationKeepsTheShape();
    testTheDecimateRecipeReadsWhatItWrites();
}
