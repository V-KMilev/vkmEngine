#define VKM_LOG_CATEGORY "IO"

#include "io/asset/cooked/asset_cook.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "vkm_log/logger.h"

#include "base/hash/fnv1a.h"
#include "io/asset/byte_stream.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/texture_asset.h"
#include "io/whole_file_write.h"

namespace Vkm::Engine::AssetCook {

namespace {

constexpr char     MAGIC[4]        = {'V', 'K', 'M', 'C'};
constexpr uint32_t ENDIAN_SENTINEL = 0x01020304u;

// What a body holds as it sits in memory: a change to one of these is a change of format.
static_assert(sizeof(Vertex) == 48, "Vertex layout changed - bump COOKER_VERSION");
static_assert(sizeof(SkinVertex) == 12, "SkinVertex layout changed - bump COOKER_VERSION");
static_assert(sizeof(BonePose) == 40, "BonePose layout changed - bump COOKER_VERSION");
static_assert(sizeof(ClipBone) == 24, "ClipBone layout changed - bump COOKER_VERSION");

/**
 * @brief A bone's row of a skeleton body's table; the names follow the table, one after another.
 */
struct BoneRecord {
    int32_t  parent  = 0;
    uint32_t nameLen = 0;
};

/**
 * @brief A marker's row of a clip body's table; the names follow the table, one after another.
 */
struct MarkerRecord {
    float    time    = 0.0f;
    uint32_t nameLen = 0;
};
static_assert(sizeof(BoneRecord) == 8 && sizeof(MarkerRecord) == 8, "a record is its two fields alone");

/**
 * @brief The header as written, before anything has judged it.
 */
struct CookedHeader {
    uint32_t sentinel      = 0;
    uint16_t assetKind     = 0;
    uint16_t cookerVersion = 0;
    uint64_t payloadBytes  = 0;
};

// How far the header got before it stopped making sense. Kind and version are not judged here: a reader
// refuses a mismatch and says why, while isCookedCurrent must stay silent.
enum class HeaderRead { Ok, BadMagic, ForeignEndian, Truncated };

HeaderRead readHeaderFields(std::istream& is, CookedHeader& out) {
    ByteReader r(is, static_cast<uint64_t>(HEADER_BYTES));
    char magic[4] = {};
    if (!r.raw(magic) || std::memcmp(magic, MAGIC, 4) != 0) return HeaderRead::BadMagic;
    if (!r.raw(out.sentinel))                                return HeaderRead::Truncated;
    if (out.sentinel != ENDIAN_SENTINEL)                     return HeaderRead::ForeignEndian;
    if (!r.raw(out.assetKind) || !r.raw(out.cookerVersion)
        || !r.raw(out.payloadBytes)) return HeaderRead::Truncated;
    return HeaderRead::Ok;
}

// Reads and validates the header, leaving the get pointer at the body start.
bool readHeader(
    std::istream& is,
    const std::filesystem::path& path,
    uint16_t expectKind,
    uint64_t& outPayloadBytes
) {
    const std::string p = path.string();
    CookedHeader header;
    switch (readHeaderFields(is, header)) {
        case HeaderRead::Ok: break;
        case HeaderRead::BadMagic:
            LOG_ERROR("Cooked asset '%s': bad magic", p.c_str());
            return false;
        case HeaderRead::ForeignEndian:
            LOG_ERROR("Cooked asset '%s': endian/sentinel mismatch (0x%08x)", p.c_str(), header.sentinel);
            return false;
        case HeaderRead::Truncated:
            LOG_ERROR("Cooked asset '%s': truncated header", p.c_str());
            return false;
    }
    if (header.assetKind != expectKind) {
        LOG_ERROR(
            "Cooked asset '%s': wrong asset kind %u (expected %u)",
            p.c_str(),
            header.assetKind,
            expectKind
        );
        return false;
    }
    if (header.cookerVersion != COOKER_VERSION) {
        LOG_ERROR(
            "Cooked asset '%s': baked by cooker version %u, not %u",
            p.c_str(),
            header.cookerVersion,
            COOKER_VERSION
        );
        return false;
    }
    outPayloadBytes = header.payloadBytes;
    return true;
}

// Whether the bytes after the header are exactly the declared payload. Silent; leaves the get pointer at
// the end. Measured by subtraction: payloadBytes is unvouched for, and adding the header to a count near
// the top of the range overflows a signed file offset.
bool payloadFillsFile(std::istream& is, uint64_t payloadBytes, std::streamoff& outFileSize) {
    is.seekg(0, std::ios::end);
    outFileSize = is.tellg();
    return outFileSize >= HEADER_BYTES
        && payloadBytes == static_cast<uint64_t>(outFileSize - HEADER_BYTES);
}

// The bytes after the header must equal the declared payload, so a corrupt count cannot drive an
// oversized allocation. Repositions the get pointer to the body start.
bool verifyFileSize(std::istream& is, const std::filesystem::path& path, uint64_t payloadBytes) {
    std::streamoff fileSize = 0;
    const bool fills = payloadFillsFile(is, payloadBytes, fileSize);
    is.seekg(HEADER_BYTES, std::ios::beg);
    if (!fills) {
        LOG_ERROR(
            "Cooked asset '%s': size mismatch (file %lld, header %lld + payload %llu)",
            path.string().c_str(),
            static_cast<long long>(fileSize),
            static_cast<long long>(HEADER_BYTES),
            static_cast<unsigned long long>(payloadBytes)
        );
        return false;
    }
    return true;
}

/**
 * @brief Open the cooked file at @p path at its body, once its header has passed.
 *
 * The header must name @p expectKind and this cooker, and the file hold exactly the payload it
 * declares, so a reader bounded by that payload cannot be asked for bytes that are not there.
 *
 * @param is           Opened on the file, at the body's start.
 * @param path         The cooked file.
 * @param expectKind   The kind tag it must carry.
 * @param what         The kind, for the log.
 * @param payloadBytes The body's length.
 * @return False, having logged, when the file is not one this build reads as @p expectKind.
 */
bool openCookedRead(
    std::ifstream& is,
    const std::filesystem::path& path,
    uint16_t expectKind,
    const char* what,
    uint64_t& payloadBytes
) {
    is.open(path, std::ios::binary);
    if (!is) {
        LOG_ERROR("Cooked %s '%s': cannot open", what, path.string().c_str());
        return false;
    }
    return readHeader(is, path, expectKind, payloadBytes) && verifyFileSize(is, path, payloadBytes);
}

/**
 * @brief Write @p body to @p path under a header naming @p assetKind, this cooker and the body's length.
 *
 * @param path      Where the file goes, already named for its key.
 * @param assetKind The kind tag it carries.
 * @param body      What the kind's writer built.
 * @param what      What the file is, for the log.
 * @return False on any IO error.
 */
bool writeCookedFile(
    const std::filesystem::path& path,
    uint16_t assetKind,
    const std::vector<uint8_t>& body,
    const char* what
) {
    std::vector<uint8_t> header;
    ByteWriter w(header);
    w.raw(MAGIC);
    w.raw(ENDIAN_SENTINEL);
    w.raw(assetKind);
    w.raw(COOKER_VERSION);
    w.raw(static_cast<uint64_t>(body.size()));

    WholeFileWrite cooked(path, std::ios::binary, what);
    if (!cooked) return false;
    std::ofstream& os = cooked.stream();
    os.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    os.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    return cooked.commit();
}

// Whether @p fault, the first of its kind's rules a cooked asset breaks, is none; logged when not. The writer
// and the reader both hold a file to it, so nothing cooks that will not read.
bool isSound(const std::string& fault, const char* what, const std::filesystem::path& path) {
    if (fault.empty()) return true;
    LOG_ERROR("Cooked %s '%s' is unsound - %s", what, path.string().c_str(), fault.c_str());
    return false;
}

// Whether a byte read off disk names one of @p E's enumerators, which run from 0 to Count.
template<typename E>
bool namesEnumerator(E value) {
    using Byte = std::underlying_type_t<E>;
    return static_cast<Byte>(value) < static_cast<Byte>(E::Count);
}

/**
 * @brief Whether every enum byte of a texture read off disk names an enumerator.
 *
 * @param params The texture's description as read.
 * @return False when any byte names none.
 */
bool namesEnumerators(const TextureParams& params) {
    return namesEnumerator(params.internalFormat) && namesEnumerator(params.format)
        && namesEnumerator(params.type) && namesEnumerator(params.wrapS) && namesEnumerator(params.wrapT)
        && namesEnumerator(params.filterOverride);
}

} // namespace

bool writeMesh(const std::filesystem::path& path, const MeshAsset& mesh) {
    if (!isSound(findMeshFault(mesh), "mesh", path)) return false;

    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.raw(mesh.boundsMin);
    w.raw(mesh.boundsMax);
    w.raw(static_cast<uint64_t>(mesh.vertices.size()));
    w.raw(static_cast<uint64_t>(mesh.indices.size()));
    w.raw(static_cast<uint64_t>(mesh.skin.size()));
    w.raw(static_cast<uint64_t>(mesh.lightmapUVs.size()));
    w.raw(mesh.skinRadius);
    w.raw(static_cast<uint32_t>(mesh.skeleton.size()));
    w.bulk(mesh.vertices);
    w.bulk(mesh.indices);
    w.bulk(mesh.skin);
    w.bulk(mesh.lightmapUVs);
    w.chars(mesh.skeleton);
    return writeCookedFile(path, COOKED_KIND<MeshAsset>, body, "Cooked mesh");
}

uint64_t cacheKey(uint64_t recipeHash, AssetType type) {
    const uint16_t kind = cookedKindOf(type);
    if (kind == 0) return recipeHash;
    const uint16_t tag[2] = {kind, COOKER_VERSION};
    return fnv1a64Bytes(tag, sizeof(tag), recipeHash);
}

bool isCookedCurrent(AssetType type, const std::filesystem::path& path) {
    const uint16_t expectKind = cookedKindOf(type);
    if (expectKind == 0) return false;

    std::ifstream is(path, std::ios::binary);
    if (!is) return false;

    // The name carried recipe, cooker and layout. The kind is cheap and catches a composed-path mistake,
    // which a hash cannot.
    CookedHeader header;
    if (readHeaderFields(is, header) != HeaderRead::Ok) return false;
    if (header.assetKind != expectKind) return false;

    // The cooker never writes a short file, but it is not the only thing that puts one on a disk.
    std::streamoff fileSize = 0;
    return payloadFillsFile(is, header.payloadBytes, fileSize);
}

bool readMesh(const std::filesystem::path& path, MeshAsset& out) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, COOKED_KIND<MeshAsset>, "mesh", payloadBytes)) return false;

    ByteReader r(is, payloadBytes);
    MeshAsset mesh;
    uint64_t vertexCount = 0;
    uint64_t indexCount  = 0;
    uint64_t skinCount   = 0;
    uint64_t uvCount     = 0;
    uint32_t skeletonNameLen = 0;
    if (!r.raw(mesh.boundsMin) || !r.raw(mesh.boundsMax)
        || !r.raw(vertexCount) || !r.raw(indexCount) || !r.raw(skinCount) || !r.raw(uvCount)
        || !r.raw(mesh.skinRadius) || !r.raw(skeletonNameLen)
        || !r.bulk(mesh.vertices, vertexCount) || !r.bulk(mesh.indices, indexCount)
        || !r.bulk(mesh.skin, skinCount) || !r.bulk(mesh.lightmapUVs, uvCount)
        || !r.chars(mesh.skeleton, skeletonNameLen) || r.left() != 0) {
        LOG_ERROR("Cooked mesh '%s': its counts and its length disagree", p.c_str());
        return false;
    }
    if (!isSound(findMeshFault(mesh), "mesh", path)) return false;

    out.boundsMin   = mesh.boundsMin;
    out.boundsMax   = mesh.boundsMax;
    out.skinRadius  = mesh.skinRadius;
    out.vertices    = std::move(mesh.vertices);
    out.indices     = std::move(mesh.indices);
    out.skin        = std::move(mesh.skin);
    out.lightmapUVs = std::move(mesh.lightmapUVs);
    out.skeleton    = std::move(mesh.skeleton);
    return true;
}

bool writeTexture(const std::filesystem::path& path, const TextureAsset& texture) {
    if (!isSound(findTextureFault(texture), "texture", path)) return false;

    const TextureParams& tp = texture.params;
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.raw(tp.width);
    w.raw(tp.height);
    w.raw(tp.internalFormat);
    w.raw(tp.format);
    w.raw(tp.type);
    w.raw(tp.wrapS);
    w.raw(tp.wrapT);
    w.raw(tp.filterOverride);
    w.raw(static_cast<uint8_t>(tp.generateMipmaps));
    w.raw(tp.mipLevels);
    w.raw(static_cast<uint64_t>(texture.pixelData.size()));
    w.bulk(texture.pixelData);
    return writeCookedFile(path, COOKED_KIND<TextureAsset>, body, "Cooked texture");
}

bool readTexture(const std::filesystem::path& path, TextureAsset& out) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, COOKED_KIND<TextureAsset>, "texture", payloadBytes)) return false;

    ByteReader r(is, payloadBytes);
    TextureAsset   texture;
    TextureParams& tp = texture.params;
    uint8_t generateMipmaps = 0;
    uint64_t pixelBytes = 0;
    if (!r.raw(tp.width) || !r.raw(tp.height)
        || !r.raw(tp.internalFormat) || !r.raw(tp.format) || !r.raw(tp.type)
        || !r.raw(tp.wrapS) || !r.raw(tp.wrapT) || !r.raw(tp.filterOverride)
        || !r.raw(generateMipmaps) || !r.raw(tp.mipLevels) || !r.raw(pixelBytes)
        || !r.bulk(texture.pixelData, pixelBytes) || r.left() != 0) {
        LOG_ERROR("Cooked texture '%s': its counts and its length disagree", p.c_str());
        return false;
    }
    tp.generateMipmaps = (generateMipmaps != 0);

    if (!namesEnumerators(tp)) {
        LOG_ERROR("Cooked texture '%s': a format byte names no enumerator", p.c_str());
        return false;
    }

    if (!isSound(findTextureFault(texture), "texture", path)) return false;

    out.params    = tp;
    out.pixelData = std::move(texture.pixelData);
    return true;
}

bool writeSkeleton(const std::filesystem::path& path, const SkeletonAsset& skeleton) {
    // The bulk writes read a bone's worth of each array: a broken invariant would be an out-of-bounds
    // read rather than a rejected cook.
    if (!isSound(findSkeletonFault(skeleton), "skeleton", path)) return false;

    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.raw(static_cast<uint64_t>(skeleton.bones.size()));
    for (const Bone& bone : skeleton.bones) {
        w.raw(BoneRecord{bone.parent, static_cast<uint32_t>(bone.name.size())});
    }
    w.bulk(skeleton.inverseBind);
    w.bulk(skeleton.bindPose);
    for (const Bone& bone : skeleton.bones) w.chars(bone.name);
    return writeCookedFile(path, COOKED_KIND<SkeletonAsset>, body, "Cooked skeleton");
}

bool readSkeleton(const std::filesystem::path& path, SkeletonAsset& out) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, COOKED_KIND<SkeletonAsset>, "skeleton", payloadBytes)) return false;

    ByteReader r(is, payloadBytes);
    SkeletonAsset skeleton;
    uint64_t boneCount = 0;
    std::vector<BoneRecord> records;
    bool read = r.raw(boneCount) && r.bulk(records, boneCount)
        && r.bulk(skeleton.inverseBind, boneCount) && r.bulk(skeleton.bindPose, boneCount);
    skeleton.bones.resize(records.size());
    for (size_t i = 0; read && i < records.size(); ++i) {
        skeleton.bones[i].parent = records[i].parent;
        read                     = r.chars(skeleton.bones[i].name, records[i].nameLen);
    }
    if (!read || r.left() != 0) {
        LOG_ERROR("Cooked skeleton '%s': its counts and its length disagree", p.c_str());
        return false;
    }
    if (!isSound(findSkeletonFault(skeleton), "skeleton", path)) return false;

    out.bones       = std::move(skeleton.bones);
    out.inverseBind = std::move(skeleton.inverseBind);
    out.bindPose    = std::move(skeleton.bindPose);
    return true;
}

bool writeAnimationClip(const std::filesystem::path& path, const AnimationClipAsset& clip) {
    // Refused, not repaired: moving a marker into the timeline or clipping a channel makes another clip.
    if (!isSound(findClipFault(clip), "clip", path)) return false;

    uint64_t markerNameBytes = 0;
    for (const ClipMarker& marker : clip.markers) markerNameBytes += marker.name.size();

    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.raw(static_cast<uint64_t>(clip.bones.size()));
    w.raw(clip.duration);
    w.raw(static_cast<uint64_t>(clip.positionTimes.size()));
    w.raw(static_cast<uint64_t>(clip.positions.size()));
    w.raw(static_cast<uint64_t>(clip.rotationTimes.size()));
    w.raw(static_cast<uint64_t>(clip.rotations.size()));
    w.raw(static_cast<uint64_t>(clip.scaleTimes.size()));
    w.raw(static_cast<uint64_t>(clip.scales.size()));
    w.raw(static_cast<uint32_t>(clip.skeleton.size()));
    w.raw(static_cast<uint64_t>(clip.markers.size()));
    w.raw(markerNameBytes);
    w.bulk(clip.bones);
    w.bulk(clip.positionTimes);
    w.bulk(clip.positions);
    w.bulk(clip.rotationTimes);
    w.bulk(clip.rotations);
    w.bulk(clip.scaleTimes);
    w.bulk(clip.scales);
    w.chars(clip.skeleton);
    for (const ClipMarker& marker : clip.markers) {
        w.raw(MarkerRecord{marker.time, static_cast<uint32_t>(marker.name.size())});
    }
    for (const ClipMarker& marker : clip.markers) w.chars(marker.name);
    return writeCookedFile(path, COOKED_KIND<AnimationClipAsset>, body, "Cooked clip");
}

bool readAnimationClip(const std::filesystem::path& path, AnimationClipAsset& out) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, COOKED_KIND<AnimationClipAsset>, "clip", payloadBytes)) return false;

    ByteReader r(is, payloadBytes);
    AnimationClipAsset clip;
    uint64_t boneCount         = 0;
    uint64_t positionTimeCount = 0;
    uint64_t positionCount     = 0;
    uint64_t rotationTimeCount = 0;
    uint64_t rotationCount     = 0;
    uint64_t scaleTimeCount    = 0;
    uint64_t scaleCount        = 0;
    uint32_t skeletonNameLen = 0;
    uint64_t markerCount     = 0;
    uint64_t markerNameBytes = 0;
    std::vector<MarkerRecord> records;
    bool read = r.raw(boneCount) && r.raw(clip.duration)
        && r.raw(positionTimeCount) && r.raw(positionCount)
        && r.raw(rotationTimeCount) && r.raw(rotationCount)
        && r.raw(scaleTimeCount) && r.raw(scaleCount)
        && r.raw(skeletonNameLen) && r.raw(markerCount) && r.raw(markerNameBytes)
        && r.bulk(clip.bones, boneCount)
        && r.bulk(clip.positionTimes, positionTimeCount) && r.bulk(clip.positions, positionCount)
        && r.bulk(clip.rotationTimes, rotationTimeCount) && r.bulk(clip.rotations, rotationCount)
        && r.bulk(clip.scaleTimes, scaleTimeCount) && r.bulk(clip.scales, scaleCount)
        && r.chars(clip.skeleton, skeletonNameLen) && r.bulk(records, markerCount);
    clip.markers.resize(records.size());
    uint64_t nameBytes = 0;
    for (size_t i = 0; read && i < records.size(); ++i) {
        clip.markers[i].time = records[i].time;
        read                 = r.chars(clip.markers[i].name, records[i].nameLen);
        nameBytes += records[i].nameLen;
    }
    if (!read || nameBytes != markerNameBytes || r.left() != 0) {
        LOG_ERROR("Cooked clip '%s': its counts and its length disagree", p.c_str());
        return false;
    }

    // Checked apart from the sizes, which two compensating corruptions could satisfy: the sampler indexes
    // these arrays directly, per bone per frame.
    if (!isSound(findClipFault(clip), "clip", path)) return false;

    out.skeleton      = std::move(clip.skeleton);
    out.duration      = clip.duration;
    out.bones         = std::move(clip.bones);
    out.markers       = std::move(clip.markers);
    out.positionTimes = std::move(clip.positionTimes);
    out.positions     = std::move(clip.positions);
    out.rotationTimes = std::move(clip.rotationTimes);
    out.rotations     = std::move(clip.rotations);
    out.scaleTimes    = std::move(clip.scaleTimes);
    out.scales        = std::move(clip.scales);
    return true;
}

bool writeAudioClip(const std::filesystem::path& path, const AudioClipAsset& audio) {
    if (!isSound(Audio::findClipFault(audio), "sound", path)) return false;

    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.raw(audio.sampleRate);
    w.raw(audio.channels);
    w.raw(static_cast<uint64_t>(audio.sampleCount()));
    if (audio.samples) w.bulk(*audio.samples);
    return writeCookedFile(path, COOKED_KIND<AudioClipAsset>, body, "Cooked sound");
}

bool readAudioClip(const std::filesystem::path& path, AudioClipAsset& out) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, COOKED_KIND<AudioClipAsset>, "sound", payloadBytes)) return false;

    ByteReader r(is, payloadBytes);
    AudioClipAsset audio;
    uint64_t sampleCount = 0;
    std::vector<int16_t> samples;
    if (!r.raw(audio.sampleRate) || !r.raw(audio.channels) || !r.raw(sampleCount)
        || !r.bulk(samples, sampleCount) || r.left() != 0) {
        LOG_ERROR("Cooked sound '%s': its counts and its length disagree", p.c_str());
        return false;
    }
    audio.samples = Audio::ClipSamples(std::move(samples));
    if (!isSound(Audio::findClipFault(audio), "sound", path)) return false;

    out.sampleRate = audio.sampleRate;
    out.channels   = audio.channels;
    out.samples    = std::move(audio.samples);
    return true;
}

} // namespace Vkm::Engine::AssetCook
