// The cooked files a runtime reads instead of source art: each kind written and read back as it
// was, and a file damaged on disk refused whole, never read past its end.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "io/asset/byte_stream.h"
#include "io/asset/cooked/asset_cook.h"
#include "io/asset/generate/mesh_generators.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/texture_asset.h"

#include "harness/check.h"
#include "harness/scratch.h"

using namespace Vkm::Audio;
using namespace Vkm::Engine;
using namespace Vkm::Test;
namespace fs = std::filesystem;

namespace {

// The header every cooked file opens with: magic, endian sentinel, kind tag, cooker version, then
// the body's length.
constexpr size_t HEADER    = static_cast<size_t>(AssetCook::HEADER_BYTES);
constexpr size_t KIND_AT   = 8;
constexpr size_t LENGTH_AT = HEADER - sizeof(uint64_t);

template<typename T>
T readAt(const std::string& bytes, size_t at) {
    T value{};
    std::memcpy(&value, bytes.data() + at, sizeof(T));
    return value;
}

template<typename T>
void writeAt(std::string& bytes, size_t at, T value) {
    std::memcpy(bytes.data() + at, &value, sizeof(T));
}

/// @p bytes with its body's length in the header set to what follows the header.
std::string withLength(std::string bytes) {
    writeAt<uint64_t>(bytes, LENGTH_AT, bytes.size() - HEADER);
    return bytes;
}

// One sound asset of each kind, using every field its file holds.

MeshAsset aMesh() {
    MeshAsset mesh = generateCube();
    mesh.skin.resize(mesh.vertices.size());
    mesh.lightmapUVs.resize(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        mesh.skin[i]        = SkinVertex{{static_cast<uint16_t>(i % 3), 2, 0, 0}, {200, 55, 0, 0}};
        mesh.lightmapUVs[i] = glm::vec2(static_cast<float>(i) / 32.0f, 0.5f);
    }
    mesh.skeleton   = "test:rig";
    mesh.skinRadius = 1.25f;
    return mesh;
}

TextureAsset aTexture() {
    TextureAsset texture;
    texture.params.width          = 4;
    texture.params.height         = 2;
    texture.params.internalFormat = TextureInternalFormat::RGBA8;
    texture.params.format         = TexturePixelFormat::RGBA;
    texture.params.wrapS          = TextureWrapMode::Repeat;
    texture.params.filterOverride = TextureFilterOverride::Nearest;
    texture.params.mipLevels      = 3;  // 4x2, 2x1, 1x1
    texture.pixelData.resize((8 + 2 + 1) * 4);
    for (size_t i = 0; i < texture.pixelData.size(); ++i) texture.pixelData[i] = static_cast<uint8_t>(i * 7);
    return texture;
}

SkeletonAsset aSkeleton(size_t bones = 3) {
    SkeletonAsset rig;
    for (size_t i = 0; i < bones; ++i) {
        rig.bones.push_back({"bone" + std::to_string(i), static_cast<int32_t>(i) - 1});
        rig.inverseBind.push_back(glm::mat4(1.0f));
        rig.inverseBind.back()[3] = glm::vec4(0.0f, -static_cast<float>(i), 0.0f, 1.0f);
        BonePose pose;
        pose.position = glm::vec3(0.0f, i == 0 ? 0.0f : 1.0f, 0.0f);
        rig.bindPose.push_back(pose);
    }
    return rig;
}

AnimationClipAsset aClip(size_t bones = 3) {
    AnimationClipAsset clip;
    clip.skeleton      = "test:rig";
    clip.duration      = 2.0f;
    clip.positionTimes = {0.0f, 1.0f, 2.0f};
    clip.positions     = {glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f)};
    clip.rotationTimes = {0.0f, 2.0f};
    clip.rotations     = {glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::quat(0.0f, 0.0f, 1.0f, 0.0f)};
    clip.scaleTimes    = {0.5f};
    clip.scales        = {glm::vec3(2.0f)};
    clip.bones.resize(bones);
    clip.bones[0].position = {0, 3};
    clip.bones[0].rotation = {0, 2};
    if (bones > 1) clip.bones[1].scale = {0, 1};
    clip.markers = {{"step", 0.25f}, {"land", 2.0f}};
    return clip;
}

AudioClipAsset aSound(uint32_t channels = 2) {
    AudioClipAsset sound;
    sound.sampleRate = 22050;
    sound.channels   = channels;
    std::vector<int16_t> samples(40 * channels);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<int16_t>(i * 300 - 6000);
    sound.samples = ClipSamples(std::move(samples));
    return sound;
}

template<typename T>
bool sameBytes(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

bool same(const MeshAsset& a, const MeshAsset& b) {
    return a.boundsMin == b.boundsMin && a.boundsMax == b.boundsMax && sameBytes(a.vertices, b.vertices)
        && a.indices == b.indices && sameBytes(a.skin, b.skin) && a.lightmapUVs == b.lightmapUVs
        && a.skeleton == b.skeleton && a.skinRadius == b.skinRadius;
}

bool same(const TextureAsset& a, const TextureAsset& b) {
    const TextureParams& p = a.params;
    const TextureParams& q = b.params;
    return p.width == q.width && p.height == q.height && p.internalFormat == q.internalFormat
        && p.format == q.format && p.type == q.type && p.wrapS == q.wrapS && p.wrapT == q.wrapT
        && p.filterOverride == q.filterOverride && p.generateMipmaps == q.generateMipmaps
        && p.mipLevels == q.mipLevels && a.pixelData == b.pixelData;
}

bool same(const SkeletonAsset& a, const SkeletonAsset& b) {
    if (a.bones.size() != b.bones.size()) return false;
    for (size_t i = 0; i < a.bones.size(); ++i) {
        if (a.bones[i].name != b.bones[i].name || a.bones[i].parent != b.bones[i].parent) return false;
    }
    return sameBytes(a.inverseBind, b.inverseBind) && sameBytes(a.bindPose, b.bindPose);
}

bool same(const AnimationClipAsset& a, const AnimationClipAsset& b) {
    if (a.markers.size() != b.markers.size()) return false;
    for (size_t i = 0; i < a.markers.size(); ++i) {
        if (a.markers[i].name != b.markers[i].name || a.markers[i].time != b.markers[i].time) return false;
    }
    return a.skeleton == b.skeleton && a.duration == b.duration && sameBytes(a.bones, b.bones)
        && a.positionTimes == b.positionTimes && a.positions == b.positions
        && a.rotationTimes == b.rotationTimes && a.rotations == b.rotations && a.scaleTimes == b.scaleTimes
        && a.scales == b.scales;
}

bool same(const AudioClipAsset& a, const AudioClipAsset& b) {
    if (a.sampleCount() != b.sampleCount()) return false;
    return a.sampleRate == b.sampleRate && a.channels == b.channels
        && (a.sampleCount() == 0 || *a.samples == *b.samples);
}

std::string faultOf(const MeshAsset& a)          { return findMeshFault(a); }
std::string faultOf(const TextureAsset& a)       { return findTextureFault(a); }
std::string faultOf(const SkeletonAsset& a)      { return findSkeletonFault(a); }
std::string faultOf(const AnimationClipAsset& a) { return findClipFault(a); }
std::string faultOf(const AudioClipAsset& a)     { return findClipFault(a); }

/**
 * @brief One cooked kind: a sound asset of it, another to stand in an `out` a read must leave
 *        alone, and its writer and reader.
 */
template<typename Asset>
struct Kind {
    const char* name;
    AssetType   type;
    Asset       good;
    Asset       untouched;
    bool (*write)(const fs::path&, const Asset&);
    bool (*read)(const fs::path&, Asset&);

    /// The bytes the writer puts on disk for @p asset.
    std::string bytesOf(const Asset& asset) const {
        const fs::path path = scratch() / (std::string(name) + ".vkmc");
        REQUIRE(write(path, asset));
        return readText(path);
    }

    /**
     * @brief Read @p bytes as this kind; on a refusal, whether `out` was left as it was passed.
     */
    struct Read {
        bool  read = false;
        bool  left = true;
        Asset out;
    };
    Read readBytes(const std::string& bytes) const {
        const fs::path path = scratch() / (std::string(name) + "_damaged.vkmc");
        writeText(path, bytes);
        Read result;
        result.out  = Asset(untouched);
        result.read = read(path, result.out);
        result.left = result.read || same(result.out, untouched);
        return result;
    }
};

Kind<MeshAsset> meshKind() {
    return {"mesh", AssetType::Mesh, aMesh(), generateSphere(
        {2}
    ), &AssetCook::writeMesh, &AssetCook::readMesh};
}
Kind<TextureAsset> textureKind() {
    TextureAsset other = aTexture();
    other.pixelData.assign(other.pixelData.size(), 9);
    return {"texture", AssetType::Texture, aTexture(), other, &AssetCook::writeTexture,
            &AssetCook::readTexture};
}
Kind<SkeletonAsset> skeletonKind() {
    return {"skeleton", AssetType::Skeleton, aSkeleton(), aSkeleton(5), &AssetCook::writeSkeleton,
            &AssetCook::readSkeleton};
}
Kind<AnimationClipAsset> clipKind() {
    return {"clip", AssetType::AnimationClip, aClip(), aClip(1), &AssetCook::writeAnimationClip,
            &AssetCook::readAnimationClip};
}
Kind<AudioClipAsset> soundKind() {
    return {"sound", AssetType::AudioClip, aSound(), aSound(1), &AssetCook::writeAudioClip,
            &AssetCook::readAudioClip};
}

/// @p body run on each kind a writer makes a file of.
template<typename Body>
void forEveryKind(Body&& body) {
    body(meshKind());
    body(textureKind());
    body(skeletonKind());
    body(clipKind());
    body(soundKind());
}

} // namespace

TEST("every kind cooks to a file that reads back as written, under its own kind's tag") {
    std::vector<uint16_t> tags;
    forEveryKind([&](const auto& kind) {
        std::printf("  %s\n", kind.name);
        const std::string bytes = kind.bytesOf(kind.good);
        const auto back = kind.readBytes(bytes);
        CHECK(back.read);
        CHECK(same(back.out, kind.good));
        // Written again from what was read, it is the same bytes: the two agree on every field.
        CHECK(kind.bytesOf(back.out) == bytes);

        const fs::path path = scratch() / (std::string(kind.name) + "_damaged.vkmc");
        CHECK(AssetCook::isCookedCurrent(kind.type, path));
        CHECK(!AssetCook::isCookedCurrent(AssetType::Material, path));
        const AssetType others[] = {
            AssetType::Mesh,
            AssetType::Texture,
            AssetType::Skeleton,
            AssetType::AnimationClip,
            AssetType::AudioClip,
            AssetType::Terrain,
        };
        for (const AssetType other : others) {
            if (other != kind.type) CHECK(!AssetCook::isCookedCurrent(other, path));
        }
        // A file its header's length does not fit is not current: a copy cut short on the way.
        writeText(path, bytes.substr(0, bytes.size() - 1));
        CHECK(!AssetCook::isCookedCurrent(kind.type, path));
        CHECK(!AssetCook::isCookedCurrent(kind.type, scratch() / "missing.vkmc"));

        tags.push_back(readAt<uint16_t>(bytes, KIND_AT));
        CHECK(tags.back() != 0);
        CHECK(tags.back() == COOKED_KIND<std::decay_t<decltype(kind.good)>>);
        // A cooked name carries its kind and the cooker: one recipe's files never share a name.
        CHECK(AssetCook::cacheKey(42, kind.type) != 42);
        CHECK(AssetCook::cacheKey(42, kind.type) != AssetCook::cacheKey(43, kind.type));
    });
    for (size_t i = 0; i < tags.size(); ++i) {
        for (size_t j = i + 1; j < tags.size(); ++j) CHECK(tags[i] != tags[j]);
    }

    // Read as another kind, a file is refused by its tag.
    MeshAsset mesh;
    CHECK(!AssetCook::readMesh(scratch() / "texture.vkmc", mesh));
    CHECK(mesh.vertices.empty());
    CHECK(countInLog("wrong asset kind") == 1);
}

TEST("a cooked file cut short or carrying bytes past its body is refused, the asset passed in left as it was") {
    forEveryKind([&](const auto& kind) {
        std::printf("  %s\n", kind.name);
        const std::string bytes = kind.bytesOf(kind.good);
        int accepted = 0, touched = 0;
        // Its header made to agree with each shorter length, so only the body can tell.
        for (size_t cut = HEADER; cut < bytes.size(); ++cut) {
            const auto damaged = kind.readBytes(withLength(bytes.substr(0, cut)));
            accepted += damaged.read;
            touched += !damaged.left;
        }
        for (const std::string& extra : {std::string(1, '\0'), std::string(7, 'x')}) {
            const auto damaged = kind.readBytes(withLength(bytes + extra));
            accepted += damaged.read;
            touched += !damaged.left;
        }
        // And a header cut short, or the wrong length left in it.
        for (size_t cut = 0; cut < HEADER; ++cut) accepted += kind.readBytes(bytes.substr(0, cut)).read;
        accepted += kind.readBytes(bytes + "z").read;
        CHECK(accepted == 0);
        CHECK(touched == 0);
    });
}

TEST("any byte of a cooked file damaged is refused or reads as an asset its kind's rules allow") {
    // From the end back: a reader that lost its bound meets a count's top byte, a vast count it
    // cannot reserve, before a merely large one.
    forEveryKind([&](const auto& kind) {
        std::printf("  %s\n", kind.name);
        const std::string bytes = kind.bytesOf(kind.good);
        int refused = 0, unsound = 0, touched = 0;
        for (size_t at = bytes.size(); at-- > HEADER;) {
            for (const uint8_t value : {uint8_t(0xFF), uint8_t(0x00), uint8_t(bytes[at] ^ 0x01)}) {
                if (static_cast<uint8_t>(bytes[at]) == value) continue;
                std::string damaged = bytes;
                damaged[at] = static_cast<char>(value);
                const auto result = kind.readBytes(damaged);
                refused += !result.read;
                unsound += result.read && !faultOf(result.out).empty();
                touched += !result.left;
            }
        }
        CHECK(refused > 0);
        CHECK(unsound == 0);
        CHECK(touched == 0);
    });
}

TEST("a cooked file whose count claims more than it holds is refused without reserving it") {
    forEveryKind([&](const auto& kind) {
        std::printf("  %s\n", kind.name);
        const std::string bytes = kind.bytesOf(kind.good);
        int unsound = 0, touched = 0;
        // A count of 2^62 laid at every place a count could be, from the end back as above.
        for (size_t at = bytes.size() - sizeof(uint64_t) + 1; at-- > HEADER;) {
            std::string damaged = bytes;
            writeAt<uint64_t>(damaged, at, uint64_t(1) << 62);
            const auto result = kind.readBytes(damaged);
            unsound += result.read && !faultOf(result.out).empty();
            touched += !result.left;
        }
        CHECK(unsound == 0);
        CHECK(touched == 0);
    });
}

TEST("an asset that breaks its kind's rules is refused by its rule, and by the writer, which leaves no file") {
    struct Row {
        std::string name;
        std::string fault;
        bool        written;
        bool        fileLeft;
    };
    std::vector<Row> rows;
    const auto judge = [&](const auto& kind, const std::string& name, const auto& asset) {
        const fs::path path = scratch() / "broken" / (name + ".vkmc");
        const bool written  = kind.write(path, asset);
        std::error_code ec;
        rows.push_back({name, faultOf(asset), written, fs::exists(path, ec)});
    };

    const auto mesh = meshKind();
    {
        MeshAsset m = aMesh();
        m.indices.pop_back();
        judge(mesh, "mesh with a partial triangle", m);
        m = aMesh(), m.skin.pop_back();
        judge(mesh, "mesh with its skin one short", m);
        m = aMesh(), m.lightmapUVs.pop_back();
        judge(mesh, "mesh with its lightmap one short", m);
        m = aMesh(), m.indices[4] = static_cast<uint32_t>(m.vertices.size());
        judge(mesh, "mesh with an index past its vertices", m);
        m = aMesh(), m.skin[3].bones[2] = SkeletonAsset::MAX_BONES;
        judge(mesh, "mesh with a bone index past a rig", m);
        m = aMesh(), m.skin[3].weights[1] -= 1;
        judge(mesh, "mesh whose skin weights sum short of 255", m);
        m = aMesh(), m.skinRadius = std::numeric_limits<float>::quiet_NaN();
        judge(mesh, "mesh with a skin radius of NaN", m);
        m = aMesh(), m.skinRadius = -1.0f;
        judge(mesh, "mesh with a negative skin radius", m);
    }
    const auto texture = textureKind();
    {
        TextureAsset t = aTexture();
        t.params.mipLevels = 2;
        t.pixelData.resize((8 + 2) * 4);
        judge(texture, "texture with a partial chain", t);
        t = aTexture(), t.pixelData.pop_back();
        judge(texture, "texture one byte short", t);
        t = aTexture(), t.pixelData.push_back(0);
        judge(texture, "texture one byte over", t);
        t = aTexture(), t.params.width = 0;
        judge(texture, "texture with no width", t);
    }
    const auto skeleton = skeletonKind();
    {
        judge(skeleton, "rig with more bones than a rig may have", aSkeleton(SkeletonAsset::MAX_BONES + 1));
        SkeletonAsset s = aSkeleton();
        s.inverseBind.pop_back();
        judge(skeleton, "rig with an inverse bind short", s);
        s = aSkeleton(), s.bones[1].parent = 1;
        judge(skeleton, "rig whose bone is its own parent", s);
        s = aSkeleton(), s.bones[2].parent = -2;
        judge(skeleton, "rig with a parent before the first", s);
    }
    const auto clip = clipKind();
    {
        judge(clip, "clip with more bones than a rig may have", aClip(SkeletonAsset::MAX_BONES + 1));
        AnimationClipAsset c = aClip();
        c.duration = std::numeric_limits<float>::quiet_NaN();
        judge(clip, "clip lasting NaN seconds", c);
        c = aClip(), c.rotations.pop_back();
        judge(clip, "clip with a key time and no value", c);
        c = aClip(), c.bones[1].scale = {1, 1};
        judge(clip, "clip with a channel past its keys", c);
        c = aClip(), c.markers[0].time = 2.5f;
        judge(clip, "clip with a marker past its end", c);
    }
    const auto sound = soundKind();
    {
        judge(sound, "sound of no channels", aSound(0));
        judge(sound, "sound of nine channels", aSound(AudioClipAsset::MAX_CHANNELS + 1));
        AudioClipAsset a = aSound();
        a.sampleRate = 0;
        judge(sound, "sound at no rate", a);
        a = aSound(), a.sampleRate = AudioClipAsset::MAX_SAMPLE_RATE + 1;
        judge(sound, "sound past the highest rate", a);
        a = aSound(), a.samples = ClipSamples(std::vector<int16_t>(81, 0));
        judge(sound, "stereo sound with half a frame", a);
    }

    for (const Row& row : rows) {
        std::printf("  %s: %s\n", row.name.c_str(), row.fault.c_str());
        CHECK(!row.fault.empty());
        CHECK(!row.written);
        CHECK(!row.fileLeft);
    }
    std::error_code ec;
    const bool anyLeft = fs::exists(scratch() / "broken", ec) && !fs::is_empty(scratch() / "broken", ec);
    CHECK(!anyLeft);

    // And the sound one of each kind passes all three.
    forEveryKind([&](const auto& kind) {
        CHECK(faultOf(kind.good).empty());
        CHECK(kind.readBytes(kind.bytesOf(kind.good)).read);
    });
}

TEST("a cooked texture whose format bytes name no enumerator is refused") {
    // Every field at its enum's last value, which must read.
    TextureAsset last;
    last.params.width          = 4;
    last.params.height         = 4;
    last.params.internalFormat = TextureInternalFormat::BC7SRGBA;
    last.params.format         = TexturePixelFormat::RGBA;
    last.params.type           = TexturePixelType::HalfFloat;
    last.params.wrapS          = TextureWrapMode::ClampToBorder;
    last.params.wrapT          = TextureWrapMode::ClampToBorder;
    last.params.filterOverride = TextureFilterOverride::Nearest;
    last.pixelData.assign(16, 7);
    const auto kind = textureKind();
    const std::string bytes = kind.bytesOf(last);
    REQUIRE(kind.readBytes(bytes).read);

    // Each field's byte found by writing a texture that differs in that field alone.
    struct Field {
        const char*                         name;
        uint8_t                             count;
        std::function<void(TextureParams&)> change;
    };
    const std::vector<Field> fields = {
        {"internal format", static_cast<uint8_t>(TextureInternalFormat::Count),
         [](TextureParams& p) { p.internalFormat = TextureInternalFormat::BC7RGBA; }},
        {"pixel format", static_cast<uint8_t>(TexturePixelFormat::Count),
         [](TextureParams& p) { p.format = TexturePixelFormat::RG; }},
        {"pixel type", static_cast<uint8_t>(TexturePixelType::Count),
         [](TextureParams& p) { p.type = TexturePixelType::Float; }},
        {"wrap s", static_cast<uint8_t>(TextureWrapMode::Count),
         [](TextureParams& p) { p.wrapS = TextureWrapMode::Repeat; }},
        {"wrap t", static_cast<uint8_t>(TextureWrapMode::Count),
         [](TextureParams& p) { p.wrapT = TextureWrapMode::MirroredRepeat; }},
        {"filter", static_cast<uint8_t>(TextureFilterOverride::Count),
         [](TextureParams& p) { p.filterOverride = TextureFilterOverride::None; }},
    };
    std::vector<size_t> places;
    for (const Field& field : fields) {
        std::printf("  %s\n", field.name);
        TextureAsset other = last;
        field.change(other.params);
        const std::string changed = kind.bytesOf(other);
        REQUIRE(changed.size() == bytes.size());
        std::vector<size_t> differ;
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] != changed[i]) differ.push_back(i);
        }
        REQUIRE(differ.size() == 1);
        const size_t at = differ[0];
        CHECK(static_cast<uint8_t>(bytes[at]) == field.count - 1);
        places.push_back(at);

        for (const uint8_t value : {field.count, uint8_t(field.count + 1), uint8_t(255)}) {
            std::string damaged = bytes;
            damaged[at] = static_cast<char>(value);
            const auto result = kind.readBytes(damaged);
            CHECK(!result.read);
            CHECK(result.left);
        }
    }
    for (size_t i = 0; i < places.size(); ++i) {
        for (size_t j = i + 1; j < places.size(); ++j) CHECK(places[i] != places[j]);
    }
    CHECK(countInLog("names no enumerator") == 18);
}

TEST("a byte reader never reads or reserves past its run, from memory or a stream") {
    const std::vector<uint8_t> run(20, 1);
    {
        ByteReader reader(run.data(), run.size());
        std::vector<uint32_t> values;
        CHECK(!reader.bulk(values, uint64_t(1) << 62));
        CHECK(values.empty());
        CHECK(!reader.bulk(values, 6));
        CHECK(values.empty());
        std::string text;
        CHECK(!reader.chars(text, 21));
        CHECK(text.empty());
        CHECK(reader.left() == 20);
        CHECK(reader.bulk(values, 5));
        CHECK(values.size() == 5);
        uint8_t past = 0;
        CHECK(!reader.raw(past));
        CHECK(reader.left() == 0);
    }
    {
        // A stream the length promised more of than it holds: once it fails, nothing is left.
        std::istringstream stream(std::string(10, 'x'));
        ByteReader reader(stream, 16);
        std::vector<uint8_t> bytes;
        CHECK(!reader.bulk(bytes, uint64_t(1) << 62));
        CHECK(bytes.empty());
        uint64_t first = 0, second = 0;
        CHECK(reader.raw(first));
        CHECK(reader.left() == 8);
        CHECK(!reader.raw(second));
        CHECK(reader.left() == 0);
    }
}
