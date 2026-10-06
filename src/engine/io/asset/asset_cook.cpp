#define VKM_LOG_CATEGORY "IO"

#include "io/asset/asset_cook.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "logger.h"

#include "core/fnv1a.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine::AssetCook {

namespace {

constexpr char     MAGIC[4]        = {'V', 'K', 'M', 'C'};
constexpr uint32_t ENDIAN_SENTINEL = 0x01020304u;
constexpr uint16_t KIND_MESH       = 1;
constexpr uint16_t KIND_TEXTURE    = 2;
constexpr uint16_t KIND_SKELETON   = 3;
constexpr uint16_t KIND_CLIP       = 4;
constexpr uint16_t KIND_AUDIO      = 5;

// Field by field, never as a struct, so padding cannot leak into the format:
// magic[4] + sentinel + kind + version + payloadBytes.
constexpr std::streamoff HEADER_BYTES = 4 + sizeof(uint32_t) + sizeof(uint16_t) * 2 + sizeof(uint64_t);

// Mesh body: boundsMin + boundsMax + vertexCount + indexCount + skinCount + skinRadius + skeletonNameLen,
// then bulk vertices, indices, skin and the name.
constexpr uint64_t MESH_FIXED_BYTES = sizeof(glm::vec3) * 2 + sizeof(uint64_t) * 3
    + sizeof(float) + sizeof(uint32_t);
// Texture body: 2*u32 params + 6 enum bytes + the mipmap flag + the level count + pixelBytes field.
constexpr uint64_t TEXTURE_FIXED_BYTES = sizeof(uint32_t) * 2 + 6 + 1 + sizeof(uint32_t) + sizeof(uint64_t);
// Skeleton body: boneCount, then per bone a {parent, nameLen} record, an inverse-bind matrix and a
// bind-pose TRS, then the concatenated name bytes.
constexpr uint64_t SKELETON_FIXED_BYTES    = sizeof(uint64_t);
constexpr uint64_t SKELETON_PER_BONE_BYTES = sizeof(int32_t) + sizeof(uint32_t)
    + sizeof(glm::mat4) + sizeof(Transform);
// Clip body: boneCount + duration + the six key-array counts + skeletonNameLen + markerCount +
// markerNameBytes, then the bulk ClipBone table, the six key arrays, the skeleton name, a {time, nameLen}
// record per marker, and the concatenated marker names.
constexpr uint64_t CLIP_FIXED_BYTES = sizeof(uint64_t) + sizeof(float)
    + sizeof(uint64_t) * 6 + sizeof(uint32_t)
    + sizeof(uint64_t) * 2;
constexpr uint64_t CLIP_PER_MARKER_BYTES = sizeof(float) + sizeof(uint32_t);
// Audio body: sampleRate + channels + sampleCount, then the interleaved PCM.
constexpr uint64_t AUDIO_FIXED_BYTES = sizeof(uint32_t) * 2 + sizeof(uint64_t);

template<typename T>
void writeRaw(std::ostream& os, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "writeRaw needs a trivially-copyable type");
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template<typename T>
bool readRaw(std::istream& is, T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "readRaw needs a trivially-copyable type");
    return static_cast<bool>(is.read(reinterpret_cast<char*>(&v), sizeof(T)));
}

// Bound a count by division against the remaining payload before anything multiplies it, and consume
// its bytes, so the size math cannot wrap and resize() never sees a bogus count from a corrupt file.
bool takeCount(
    uint64_t count,
    uint64_t elementBytes,
    uint64_t& remaining,
    const std::string& path,
    const char* what,
    const char* field
) {
    if (count > remaining / elementBytes) {
        LOG_ERROR(
            "Cooked %s '%s': implausible %s count %llu",
            what,
            path.c_str(),
            field,
            static_cast<unsigned long long>(count)
        );
        return false;
    }
    remaining -= count * elementBytes;
    return true;
}

template<typename T>
void writeBulk(std::ostream& os, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>, "writeBulk needs a trivially-copyable type");
    if (!values.empty()) os.write(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(T));
}

template<typename T>
void readBulk(std::istream& is, std::vector<T>& values, uint64_t count) {
    static_assert(std::is_trivially_copyable_v<T>, "readBulk needs a trivially-copyable type");
    values.resize(static_cast<size_t>(count));
    if (count) is.read(reinterpret_cast<char*>(values.data()), count * sizeof(T));
}

void writeHeader(std::ostream& os, uint16_t assetKind, uint64_t payloadBytes) {
    os.write(MAGIC, 4);
    writeRaw(os, ENDIAN_SENTINEL);
    writeRaw(os, assetKind);
    writeRaw(os, COOKER_VERSION);
    writeRaw(os, payloadBytes);
}

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
    char magic[4] = {};
    if (!is.read(magic, 4) || std::memcmp(magic, MAGIC, 4) != 0) return HeaderRead::BadMagic;
    if (!readRaw(is, out.sentinel))                              return HeaderRead::Truncated;
    if (out.sentinel != ENDIAN_SENTINEL)                         return HeaderRead::ForeignEndian;
    if (!readRaw(is, out.assetKind) || !readRaw(is, out.cookerVersion)
        || !readRaw(is, out.payloadBytes)) return HeaderRead::Truncated;
    return HeaderRead::Ok;
}

// The kind tag a type's cooked file must carry, or zero for a material: its recipe is its runtime form.
uint16_t cookedKind(AssetType type) {
    switch (type) {
        case AssetType::Mesh:          return KIND_MESH;
        case AssetType::Texture:       return KIND_TEXTURE;
        case AssetType::Skeleton:      return KIND_SKELETON;
        case AssetType::AnimationClip: return KIND_CLIP;
        case AssetType::AudioClip:     return KIND_AUDIO;
        case AssetType::Material:
        case AssetType::Count:         break;
    }
    return 0;
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

// Where a cooked write goes until it is whole.
std::filesystem::path tempFor(const std::filesystem::path& path) {
    return std::filesystem::path(path).concat(".tmp");
}

// Create parent dirs and open a temporary beside `path`; the stream is unopened on failure. The artifact
// wears its own name only once whole.
std::ofstream openCookedWrite(const std::filesystem::path& path, const char* what) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream os(tempFor(path), std::ios::binary | std::ios::trunc);
    if (!os) LOG_ERROR("Cooked %s '%s': cannot open for writing", what, path.string().c_str());
    return os;
}

// Rename the temporary onto the artifact's name: atomic within a directory, so the file is absent or
// complete.
bool commitCookedWrite(std::ofstream& os, const std::filesystem::path& path, const char* what) {
    os.close();

    const std::filesystem::path temp = tempFor(path);
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        LOG_ERROR("Cooked %s '%s': cannot publish (%s)", what, path.string().c_str(), ec.message().c_str());
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

// Open `path` and validate header and declared size against kind, cooker version and fixed-body size.
// On success `is` is at the body start.
bool openCookedRead(
    std::ifstream& is,
    const std::filesystem::path& path,
    uint16_t expectKind,
    uint64_t fixedBytes,
    const char* what,
    uint64_t& outPayloadBytes
) {
    is.open(path, std::ios::binary);
    if (!is) {
        LOG_ERROR("Cooked %s '%s': cannot open", what, path.string().c_str());
        return false;
    }
    if (!readHeader(is, path, expectKind, outPayloadBytes)) return false;
    if (!verifyFileSize(is, path, outPayloadBytes)) return false;
    if (outPayloadBytes < fixedBytes) {
        LOG_ERROR("Cooked %s '%s': payload too small", what, path.string().c_str());
        return false;
    }
    return true;
}

} // namespace

bool writeMesh(const std::filesystem::path& path, const MeshAsset& mesh) {
    static_assert(sizeof(Vertex) == 48, "Vertex layout changed - bump COOKER_VERSION");
    static_assert(std::is_trivially_copyable_v<Vertex>, "Vertex must be trivially copyable to bulk-write");

    const uint64_t vertexCount = mesh.vertices.size();
    const uint64_t indexCount  = mesh.indices.size();
    const uint64_t skinCount   = mesh.skin.size();
    // The skin stream is parallel to the vertices or absent: the vertex stage reads both by one index.
    if (skinCount != 0 && skinCount != vertexCount) {
        LOG_ERROR(
            "Cooked mesh '%s': %llu skin entries against %llu vertices",
            path.string().c_str(),
            static_cast<unsigned long long>(skinCount),
            static_cast<unsigned long long>(vertexCount)
        );
        return false;
    }

    std::ofstream os = openCookedWrite(path, "mesh");
    if (!os) return false;

    const uint64_t payloadBytes = MESH_FIXED_BYTES
        + vertexCount * sizeof(Vertex) + indexCount * sizeof(uint32_t)
        + skinCount * sizeof(SkinVertex) + mesh.skeleton.size();

    writeHeader(os, KIND_MESH, payloadBytes);
    writeRaw(os, mesh.boundsMin);
    writeRaw(os, mesh.boundsMax);
    writeRaw(os, vertexCount);
    writeRaw(os, indexCount);
    writeRaw(os, skinCount);
    writeRaw(os, mesh.skinRadius);
    writeRaw(os, static_cast<uint32_t>(mesh.skeleton.size()));
    writeBulk(os, mesh.vertices);
    writeBulk(os, mesh.indices);
    writeBulk(os, mesh.skin);
    os.write(mesh.skeleton.data(), static_cast<std::streamsize>(mesh.skeleton.size()));

    if (!os) {
        LOG_ERROR("Cooked mesh '%s': write failed", path.string().c_str());
        return false;
    }
    return commitCookedWrite(os, path, "mesh");
}

uint64_t cacheKey(uint64_t recipeHash, AssetType type) {
    const uint16_t kind = cookedKind(type);
    if (kind == 0) return recipeHash;
    const uint16_t tag[2] = {kind, COOKER_VERSION};
    return fnv1a64Bytes(tag, sizeof(tag), recipeHash);
}

bool isCookedCurrent(AssetType type, const std::filesystem::path& path) {
    const uint16_t expectKind = cookedKind(type);
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
    if (!openCookedRead(is, path, KIND_MESH, MESH_FIXED_BYTES, "mesh", payloadBytes)) return false;

    glm::vec3 boundsMin{0};
    glm::vec3 boundsMax{0};
    uint64_t vertexCount = 0;
    uint64_t indexCount  = 0;
    uint64_t skinCount   = 0;
    float    skinRadius  = 0.0f;
    uint32_t skeletonNameLen = 0;
    if (!readRaw(is, boundsMin) || !readRaw(is, boundsMax)
        || !readRaw(is, vertexCount) || !readRaw(is, indexCount)
        || !readRaw(is, skinCount) || !readRaw(is, skinRadius) || !readRaw(is, skeletonNameLen)) {
        LOG_ERROR("Cooked mesh '%s': truncated body", p.c_str());
        return false;
    }

    uint64_t remaining = payloadBytes - MESH_FIXED_BYTES;
    if (!takeCount(vertexCount, sizeof(Vertex), remaining, p, "mesh", "vertex")
        || !takeCount(indexCount, sizeof(uint32_t), remaining, p, "mesh", "index")
        || !takeCount(skinCount, sizeof(SkinVertex), remaining, p, "mesh", "skin")
        || !takeCount(skeletonNameLen, 1, remaining, p, "mesh", "rig name")) return false;
    if (remaining != 0) {
        LOG_ERROR("Cooked mesh '%s': payload size inconsistent with counts", p.c_str());
        return false;
    }
    // Checked at write too: the vertex stage reads both streams by one index, so a short skin stream is
    // an out-of-bounds fetch on every draw.
    if (skinCount != 0 && skinCount != vertexCount) {
        LOG_ERROR(
            "Cooked mesh '%s': %llu skin entries against %llu vertices",
            p.c_str(),
            static_cast<unsigned long long>(skinCount),
            static_cast<unsigned long long>(vertexCount)
        );
        return false;
    }
    if (!std::isfinite(skinRadius) || skinRadius < 0.0f) {
        LOG_ERROR("Cooked mesh '%s': implausible skin radius %f", p.c_str(), static_cast<double>(skinRadius));
        return false;
    }

    out.boundsMin  = boundsMin;
    out.boundsMax  = boundsMax;
    out.skinRadius = skinRadius;
    readBulk(is, out.vertices, vertexCount);
    readBulk(is, out.indices, indexCount);
    readBulk(is, out.skin, skinCount);
    out.skeleton.resize(skeletonNameLen);
    if (skeletonNameLen) is.read(out.skeleton.data(), skeletonNameLen);
    // One check for the whole body: the counts above proved the bytes are there, so a failure is an IO
    // error, and failbit is sticky.
    if (!is) {
        LOG_ERROR("Cooked mesh '%s': body read failed", p.c_str());
        out.vertices.clear();
        out.indices.clear();
        out.skin.clear();
        return false;
    }

    // An index past the vertex count addresses a vertex the mesh does not have.
    const auto vertexTotal = static_cast<uint32_t>(out.vertices.size());
    for (const uint32_t index : out.indices) {
        if (index >= vertexTotal) {
            LOG_ERROR(
                "Cooked mesh '%s': index %u is past the %u vertices it declares",
                p.c_str(),
                index,
                vertexTotal
            );
            out.vertices.clear();
            out.indices.clear();
            out.skin.clear();
            return false;
        }
    }

    // A bone index addresses the vertex stage's pose palette: a corrupt one reads out of range every frame.
    for (const SkinVertex& skin : out.skin) {
        for (const uint16_t bone : skin.bones) {
            if (bone >= MAX_SKELETON_BONES) {
                LOG_ERROR(
                    "Cooked mesh '%s': bone index %u is past the %u a rig can hold",
                    p.c_str(),
                    bone,
                    MAX_SKELETON_BONES
                );
                out.vertices.clear();
                out.indices.clear();
                out.skin.clear();
                return false;
            }
        }
    }

    return true;
}

bool writeTexture(const std::filesystem::path& path, const TextureAsset& texture) {
    // Written as raw values, so the format IS the enumerator order, and a reorder is invisible to the
    // version check and the recipe hash. Every enumerator is named, or a swap in the middle passes.
    static_assert(
        static_cast<uint8_t>(TextureInternalFormat::R8) == 0
            && static_cast<uint8_t>(TextureInternalFormat::RG8) == 1
            && static_cast<uint8_t>(TextureInternalFormat::RGB8) == 2
            && static_cast<uint8_t>(TextureInternalFormat::RGBA8) == 3
            && static_cast<uint8_t>(TextureInternalFormat::SRGB8) == 4
            && static_cast<uint8_t>(TextureInternalFormat::SRGBA8) == 5
            && static_cast<uint8_t>(TextureInternalFormat::RGBA16F) == 6
            && static_cast<uint8_t>(TextureInternalFormat::RGBA32F) == 7
            && static_cast<uint8_t>(TextureInternalFormat::BC4R) == 8
            && static_cast<uint8_t>(TextureInternalFormat::BC5RG) == 9
            && static_cast<uint8_t>(TextureInternalFormat::BC7RGBA) == 10
            && static_cast<uint8_t>(TextureInternalFormat::BC7SRGBA) == 11,
        "TextureInternalFormat reordered - bump COOKER_VERSION"
    );
    static_assert(
        static_cast<uint8_t>(TexturePixelFormat::R) == 0
            && static_cast<uint8_t>(TexturePixelFormat::RG) == 1
            && static_cast<uint8_t>(TexturePixelFormat::RGB) == 2
            && static_cast<uint8_t>(TexturePixelFormat::RGBA) == 3,
        "TexturePixelFormat reordered - bump COOKER_VERSION"
    );
    static_assert(
        static_cast<uint8_t>(TexturePixelType::UnsignedByte) == 0
            && static_cast<uint8_t>(TexturePixelType::Float) == 1
            && static_cast<uint8_t>(TexturePixelType::HalfFloat) == 2,
        "TexturePixelType reordered - bump COOKER_VERSION"
    );
    static_assert(
        static_cast<uint8_t>(TextureWrapMode::Repeat) == 0
            && static_cast<uint8_t>(TextureWrapMode::MirroredRepeat) == 1
            && static_cast<uint8_t>(TextureWrapMode::ClampToEdge) == 2
            && static_cast<uint8_t>(TextureWrapMode::ClampToBorder) == 3,
        "TextureWrapMode reordered - bump COOKER_VERSION"
    );
    static_assert(
        static_cast<uint8_t>(TextureFilterOverride::None) == 0
            && static_cast<uint8_t>(TextureFilterOverride::Nearest) == 1,
        "TextureFilterOverride reordered - bump COOKER_VERSION"
    );

    const TextureParams& tp = texture.params;
    const uint64_t pixelBytes  = texture.pixelData.size();

    // The reader refuses a level count other than one or the whole chain, and pixels that are not exactly
    // those levels; cooking either makes a file nothing can load.
    const uint32_t chain  = mipChainLength(tp.width, tp.height);
    const bool     levels = chain != 0 && (tp.mipLevels == 1 || tp.mipLevels == chain);
    uint64_t levelBytes = 0;
    for (uint32_t level = 0; levels && level < tp.mipLevels; ++level) {
        levelBytes += textureLevelBytes(tp, level);
    }
    if (!levels || levelBytes != pixelBytes) {
        LOG_ERROR(
            "Cooked texture '%s': %llu pixel byte(s) are not %u level(s) of %ux%u",
            path.string().c_str(),
            static_cast<unsigned long long>(pixelBytes),
            tp.mipLevels,
            tp.width,
            tp.height
        );
        return false;
    }

    std::ofstream os = openCookedWrite(path, "texture");
    if (!os) return false;

    const uint64_t payloadBytes = TEXTURE_FIXED_BYTES + pixelBytes;

    writeHeader(os, KIND_TEXTURE, payloadBytes);
    writeRaw(os, tp.width);
    writeRaw(os, tp.height);
    writeRaw(os, tp.internalFormat);
    writeRaw(os, tp.format);
    writeRaw(os, tp.type);
    writeRaw(os, tp.wrapS);
    writeRaw(os, tp.wrapT);
    writeRaw(os, tp.filterOverride);
    writeRaw(os, static_cast<uint8_t>(tp.generateMipmaps));
    writeRaw(os, tp.mipLevels);
    writeRaw(os, pixelBytes);
    if (pixelBytes) os.write(reinterpret_cast<const char*>(texture.pixelData.data()), pixelBytes);

    if (!os) {
        LOG_ERROR("Cooked texture '%s': write failed", path.string().c_str());
        return false;
    }
    return commitCookedWrite(os, path, "texture");
}

bool readTexture(const std::filesystem::path& path, TextureAsset& out) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, KIND_TEXTURE, TEXTURE_FIXED_BYTES, "texture", payloadBytes)) return false;

    TextureParams tp;
    uint8_t generateMipmaps = 0;
    uint64_t pixelBytes = 0;
    if (!readRaw(is, tp.width) || !readRaw(is, tp.height)
        || !readRaw(is, tp.internalFormat) || !readRaw(is, tp.format) || !readRaw(is, tp.type)
        || !readRaw(is, tp.wrapS) || !readRaw(is, tp.wrapT) || !readRaw(is, tp.filterOverride)
        || !readRaw(is, generateMipmaps) || !readRaw(is, tp.mipLevels) || !readRaw(is, pixelBytes)) {
        LOG_ERROR("Cooked texture '%s': truncated body", p.c_str());
        return false;
    }
    tp.generateMipmaps = (generateMipmaps != 0);

    if (pixelBytes != payloadBytes - TEXTURE_FIXED_BYTES) {
        LOG_ERROR("Cooked texture '%s': pixel size inconsistent with payload", p.c_str());
        return false;
    }

    // One level, or every level to 1x1: a mipmap filter over a partial chain samples as black in GL.
    const uint32_t chain = mipChainLength(tp.width, tp.height);
    if (chain == 0 || (tp.mipLevels != 1 && tp.mipLevels != chain)) {
        LOG_ERROR(
            "Cooked texture '%s': %u level(s) for %ux%u, which has %u",
            p.c_str(),
            tp.mipLevels,
            tp.width,
            tp.height,
            chain
        );
        return false;
    }

    // Each level reaches the upload verbatim, which reads exactly its bytes from this buffer. Bounded per
    // level by division so the math cannot wrap; the levels must account for every byte.
    uint64_t remaining = pixelBytes;
    for (uint32_t level = 0; level < tp.mipLevels; ++level) {
        if (!takeCount(
            textureLevelUnits(tp, level),
            textureUnitBytes(tp),
            remaining,
            p,
            "texture",
            "level-size"
        )) return false;
    }
    if (remaining != 0) {
        LOG_ERROR(
            "Cooked texture '%s': %llu pixel byte(s) do not describe %u level(s) of %ux%u",
            p.c_str(),
            static_cast<unsigned long long>(pixelBytes),
            tp.mipLevels,
            tp.width,
            tp.height
        );
        return false;
    }

    out.params = tp;
    out.pixelData.resize(static_cast<size_t>(pixelBytes));
    if (pixelBytes && !is.read(reinterpret_cast<char*>(out.pixelData.data()), pixelBytes)) {
        LOG_ERROR("Cooked texture '%s': pixel read failed", p.c_str());
        return false;
    }

    return true;
}

bool writeSkeleton(const std::filesystem::path& path, const SkeletonAsset& skeleton) {
    static_assert(sizeof(Transform) == 40, "Transform layout changed - bump COOKER_VERSION");
    static_assert(
        std::is_trivially_copyable_v<Transform>,
        "Transform must be trivially copyable to bulk-write"
    );

    const uint64_t boneCount = skeleton.bones.size();
    // The bulk writes read boneCount elements from each array: a broken invariant would be an
    // out-of-bounds read rather than a rejected cook.
    const std::string fault = findSkeletonFault(skeleton);
    if (!fault.empty()) {
        LOG_ERROR("Cooked skeleton '%s' cannot be posed - %s", path.string().c_str(), fault.c_str());
        return false;
    }
    if (boneCount > MAX_SKELETON_BONES) {
        LOG_ERROR(
            "Cooked skeleton '%s': %llu bones is past the %u the format admits",
            path.string().c_str(),
            static_cast<unsigned long long>(boneCount),
            MAX_SKELETON_BONES
        );
        return false;
    }

    std::ofstream os = openCookedWrite(path, "skeleton");
    if (!os) return false;

    uint64_t nameBytes = 0;
    for (const Bone& bone : skeleton.bones) nameBytes += bone.name.size();
    const uint64_t payloadBytes = SKELETON_FIXED_BYTES + boneCount * SKELETON_PER_BONE_BYTES + nameBytes;

    writeHeader(os, KIND_SKELETON, payloadBytes);
    writeRaw(os, boneCount);
    for (const Bone& bone : skeleton.bones) {
        writeRaw(os, bone.parent);
        writeRaw(os, static_cast<uint32_t>(bone.name.size()));
    }
    writeBulk(os, skeleton.inverseBind);
    writeBulk(os, skeleton.bindPose);
    for (const Bone& bone : skeleton.bones) {
        os.write(bone.name.data(), static_cast<std::streamsize>(bone.name.size()));
    }

    if (!os) {
        LOG_ERROR("Cooked skeleton '%s': write failed", path.string().c_str());
        return false;
    }
    return commitCookedWrite(os, path, "skeleton");
}

bool readSkeleton(const std::filesystem::path& path, SkeletonAsset& out) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(
        is,
        path,
        KIND_SKELETON,
        SKELETON_FIXED_BYTES,
        "skeleton",
        payloadBytes
    )) return false;

    uint64_t boneCount = 0;
    if (!readRaw(is, boneCount)) {
        LOG_ERROR("Cooked skeleton '%s': truncated body", p.c_str());
        return false;
    }

    // Division, so the size math cannot wrap and a corrupt count cannot drive an oversized resize.
    const uint64_t afterFixed = payloadBytes - SKELETON_FIXED_BYTES;
    if (boneCount > afterFixed / SKELETON_PER_BONE_BYTES) {
        LOG_ERROR(
            "Cooked skeleton '%s': implausible bone count %llu",
            p.c_str(),
            static_cast<unsigned long long>(boneCount)
        );
        return false;
    }
    if (boneCount > MAX_SKELETON_BONES) {
        LOG_ERROR(
            "Cooked skeleton '%s': %llu bones is past the %u the format admits",
            p.c_str(),
            static_cast<unsigned long long>(boneCount),
            MAX_SKELETON_BONES
        );
        return false;
    }

    // What the fixed-size records leave is exactly the name blob, which bounds each name length.
    const uint64_t nameBudget = afterFixed - boneCount * SKELETON_PER_BONE_BYTES;

    out.bones.assign(static_cast<size_t>(boneCount), Bone{});
    uint64_t nameBytes = 0;
    for (uint64_t i = 0; i < boneCount; ++i) {
        int32_t  parent  = 0;
        uint32_t nameLen = 0;
        if (!readRaw(is, parent) || !readRaw(is, nameLen)) {
            LOG_ERROR("Cooked skeleton '%s': truncated bone table", p.c_str());
            out.bones.clear();
            return false;
        }
        if (nameLen > nameBudget - nameBytes) {
            LOG_ERROR(
                "Cooked skeleton '%s': bone %llu declares a %u-byte name past the payload",
                p.c_str(),
                static_cast<unsigned long long>(i),
                nameLen
            );
            out.bones.clear();
            return false;
        }
        nameBytes += nameLen;
        out.bones[static_cast<size_t>(i)].parent = parent;
        out.bones[static_cast<size_t>(i)].name.resize(nameLen);
    }
    if (nameBytes != nameBudget) {
        LOG_ERROR("Cooked skeleton '%s': payload size inconsistent with counts", p.c_str());
        out.bones.clear();
        return false;
    }

    readBulk(is, out.inverseBind, boneCount);
    readBulk(is, out.bindPose, boneCount);
    for (Bone& bone : out.bones) {
        if (!bone.name.empty()) is.read(bone.name.data(), static_cast<std::streamsize>(bone.name.size()));
    }
    // One check for the whole body: the sizes above proved the bytes are there, so a failure is an IO
    // error, and failbit is sticky.
    if (!is) {
        LOG_ERROR("Cooked skeleton '%s': body read failed", p.c_str());
        out.bones.clear();
        out.inverseBind.clear();
        out.bindPose.clear();
        return false;
    }

    const std::string fault = findSkeletonFault(out);
    if (!fault.empty()) {
        LOG_ERROR("Cooked skeleton '%s' cannot be posed - %s", p.c_str(), fault.c_str());
        out.bones.clear();
        out.inverseBind.clear();
        out.bindPose.clear();
        return false;
    }
    return true;
}

bool writeAnimationClip(const std::filesystem::path& path, const AnimationClipAsset& clip) {
    static_assert(sizeof(ClipBone) == 24, "ClipBone layout changed - bump COOKER_VERSION");
    static_assert(
        std::is_trivially_copyable_v<ClipBone>,
        "ClipBone must be trivially copyable to bulk-write"
    );

    const uint64_t boneCount = clip.bones.size();
    if (boneCount > MAX_SKELETON_BONES) {
        LOG_ERROR(
            "Cooked clip '%s': %llu bones is past the %u the format admits",
            path.string().c_str(),
            static_cast<unsigned long long>(boneCount),
            MAX_SKELETON_BONES
        );
        return false;
    }
    // Refused, not repaired: moving a marker into the timeline or clipping a channel makes another clip.
    const std::string fault = findClipFault(clip);
    if (!fault.empty()) {
        LOG_ERROR("Cooked clip '%s' cannot be played - %s", path.string().c_str(), fault.c_str());
        return false;
    }

    std::ofstream os = openCookedWrite(path, "clip");
    if (!os) return false;

    uint64_t markerNameBytes = 0;
    for (const ClipMarker& marker : clip.markers) markerNameBytes += marker.name.size();

    const uint64_t payloadBytes = CLIP_FIXED_BYTES
        + boneCount * sizeof(ClipBone)
        + clip.positionTimes.size() * sizeof(float)     + clip.positions.size() * sizeof(glm::vec3)
        + clip.rotationTimes.size() * sizeof(float)     + clip.rotations.size() * sizeof(glm::quat)
        + clip.scaleTimes.size()    * sizeof(float)     + clip.scales.size()    * sizeof(glm::vec3)
        + clip.skeleton.size()
        + clip.markers.size() * CLIP_PER_MARKER_BYTES   + markerNameBytes;

    writeHeader(os, KIND_CLIP, payloadBytes);
    writeRaw(os, boneCount);
    writeRaw(os, clip.duration);
    writeRaw(os, static_cast<uint64_t>(clip.positionTimes.size()));
    writeRaw(os, static_cast<uint64_t>(clip.positions.size()));
    writeRaw(os, static_cast<uint64_t>(clip.rotationTimes.size()));
    writeRaw(os, static_cast<uint64_t>(clip.rotations.size()));
    writeRaw(os, static_cast<uint64_t>(clip.scaleTimes.size()));
    writeRaw(os, static_cast<uint64_t>(clip.scales.size()));
    writeRaw(os, static_cast<uint32_t>(clip.skeleton.size()));
    writeRaw(os, static_cast<uint64_t>(clip.markers.size()));
    writeRaw(os, markerNameBytes);
    writeBulk(os, clip.bones);
    writeBulk(os, clip.positionTimes);
    writeBulk(os, clip.positions);
    writeBulk(os, clip.rotationTimes);
    writeBulk(os, clip.rotations);
    writeBulk(os, clip.scaleTimes);
    writeBulk(os, clip.scales);
    os.write(clip.skeleton.data(), static_cast<std::streamsize>(clip.skeleton.size()));
    for (const ClipMarker& marker : clip.markers) {
        writeRaw(os, marker.time);
        writeRaw(os, static_cast<uint32_t>(marker.name.size()));
    }
    for (const ClipMarker& marker : clip.markers) {
        os.write(marker.name.data(), static_cast<std::streamsize>(marker.name.size()));
    }

    if (!os) {
        LOG_ERROR("Cooked clip '%s': write failed", path.string().c_str());
        return false;
    }
    return commitCookedWrite(os, path, "clip");
}

bool readAnimationClip(const std::filesystem::path& path, AnimationClipAsset& out) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, KIND_CLIP, CLIP_FIXED_BYTES, "clip", payloadBytes)) return false;

    uint64_t boneCount = 0;
    float    duration  = 0.0f;
    uint64_t positionTimeCount = 0, positionCount = 0;
    uint64_t rotationTimeCount = 0, rotationCount = 0;
    uint64_t scaleTimeCount    = 0, scaleCount    = 0;
    uint32_t skeletonNameLen   = 0;
    uint64_t markerCount       = 0;
    uint64_t markerNameBytes   = 0;
    if (!readRaw(is, boneCount) || !readRaw(is, duration)
        || !readRaw(is, positionTimeCount) || !readRaw(is, positionCount)
        || !readRaw(is, rotationTimeCount) || !readRaw(is, rotationCount)
        || !readRaw(is, scaleTimeCount) || !readRaw(is, scaleCount)
        || !readRaw(is, skeletonNameLen) || !readRaw(is, markerCount)
        || !readRaw(is, markerNameBytes)) {
        LOG_ERROR("Cooked clip '%s': truncated body", p.c_str());
        return false;
    }

    // Every count is bounded in the order the arrays are written.
    uint64_t remaining = payloadBytes - CLIP_FIXED_BYTES;
    if (!takeCount(boneCount, sizeof(ClipBone), remaining, p, "clip", "bone")
        || !takeCount(positionTimeCount, sizeof(float), remaining, p, "clip", "position time")
        || !takeCount(positionCount, sizeof(glm::vec3), remaining, p, "clip", "position")
        || !takeCount(rotationTimeCount, sizeof(float), remaining, p, "clip", "rotation time")
        || !takeCount(rotationCount, sizeof(glm::quat), remaining, p, "clip", "rotation")
        || !takeCount(scaleTimeCount, sizeof(float), remaining, p, "clip", "scale time")
        || !takeCount(scaleCount, sizeof(glm::vec3), remaining, p, "clip", "scale")
        || !takeCount(skeletonNameLen, 1, remaining, p, "clip", "rig name")
        || !takeCount(markerCount, CLIP_PER_MARKER_BYTES, remaining, p, "clip", "marker")
        || !takeCount(markerNameBytes, 1, remaining, p, "clip", "marker name")) return false;
    if (remaining != 0) {
        LOG_ERROR("Cooked clip '%s': payload size inconsistent with counts", p.c_str());
        return false;
    }

    if (boneCount > MAX_SKELETON_BONES) {
        LOG_ERROR(
            "Cooked clip '%s': %llu bones is past the %u the format admits",
            p.c_str(),
            static_cast<unsigned long long>(boneCount),
            MAX_SKELETON_BONES
        );
        return false;
    }

    readBulk(is, out.bones, boneCount);
    readBulk(is, out.positionTimes, positionTimeCount);
    readBulk(is, out.positions, positionCount);
    readBulk(is, out.rotationTimes, rotationTimeCount);
    readBulk(is, out.rotations, rotationCount);
    readBulk(is, out.scaleTimes, scaleTimeCount);
    readBulk(is, out.scales, scaleCount);
    out.skeleton.resize(skeletonNameLen);
    if (skeletonNameLen) is.read(out.skeleton.data(), skeletonNameLen);

    // The fixed-size marker records are accounted for, so what is left is exactly the name blob; each
    // declared length is bounded against it before any is read.
    out.markers.assign(static_cast<size_t>(markerCount), ClipMarker{});
    uint64_t nameBytes = 0;
    for (uint64_t i = 0; i < markerCount; ++i) {
        float    time    = 0.0f;
        uint32_t nameLen = 0;
        if (!readRaw(is, time) || !readRaw(is, nameLen)) {
            LOG_ERROR("Cooked clip '%s': truncated marker table", p.c_str());
            out.markers.clear();
            return false;
        }
        if (nameLen > markerNameBytes - nameBytes) {
            LOG_ERROR(
                "Cooked clip '%s': marker %llu declares a %u-byte name past the payload",
                p.c_str(),
                static_cast<unsigned long long>(i),
                nameLen
            );
            out.markers.clear();
            return false;
        }
        nameBytes += nameLen;
        out.markers[static_cast<size_t>(i)].time = time;
        out.markers[static_cast<size_t>(i)].name.resize(nameLen);
    }
    if (nameBytes != markerNameBytes) {
        LOG_ERROR("Cooked clip '%s': payload size inconsistent with counts", p.c_str());
        out.markers.clear();
        return false;
    }
    for (ClipMarker& marker : out.markers) {
        if (!marker.name.empty()) {
            is.read(marker.name.data(), static_cast<std::streamsize>(marker.name.size()));
        }
    }

    if (!is) {
        LOG_ERROR("Cooked clip '%s': body read failed", p.c_str());
        out.bones.clear();
        out.markers.clear();
        return false;
    }

    // Checked apart from the sizes, which two compensating corruptions could satisfy: the sampler indexes
    // these arrays directly, per bone per frame.
    out.duration = duration;
    const std::string fault = findClipFault(out);
    if (!fault.empty()) {
        LOG_ERROR("Cooked clip '%s' cannot be played - %s", p.c_str(), fault.c_str());
        out.bones.clear();
        out.markers.clear();
        return false;
    }
    return true;
}

bool writeAudioClip(const std::filesystem::path& path, const AudioClipAsset& audio) {
    const std::string p = path.string();
    if (audio.channels == 0 || audio.channels > MAX_AUDIO_CHANNELS) {
        LOG_ERROR(
            "Cooked sound '%s': %u channels is outside the 1..%u the format admits",
            p.c_str(),
            audio.channels,
            MAX_AUDIO_CHANNELS
        );
        return false;
    }
    if (audio.sampleRate == 0 || audio.sampleRate > MAX_AUDIO_SAMPLE_RATE) {
        LOG_ERROR("Cooked sound '%s': implausible sample rate %u", p.c_str(), audio.sampleRate);
        return false;
    }
    // The mixer reads whole frames, so a partial last frame runs it off the end of the buffer.
    if (audio.sampleCount() % audio.channels != 0) {
        LOG_ERROR(
            "Cooked sound '%s': %zu samples do not divide into %u channels",
            p.c_str(),
            audio.sampleCount(),
            audio.channels
        );
        return false;
    }

    std::ofstream os = openCookedWrite(path, "sound");
    if (!os) return false;

    const uint64_t payloadBytes = AUDIO_FIXED_BYTES + audio.sampleCount() * sizeof(int16_t);

    writeHeader(os, KIND_AUDIO, payloadBytes);
    writeRaw(os, audio.sampleRate);
    writeRaw(os, audio.channels);
    writeRaw(os, static_cast<uint64_t>(audio.sampleCount()));
    if (audio.samples) writeBulk(os, *audio.samples);

    if (!os) {
        LOG_ERROR("Cooked sound '%s': write failed", p.c_str());
        return false;
    }
    return commitCookedWrite(os, path, "sound");
}

bool readAudioClip(const std::filesystem::path& path, AudioClipAsset& out) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, KIND_AUDIO, AUDIO_FIXED_BYTES, "sound", payloadBytes)) return false;

    uint32_t sampleRate  = 0;
    uint32_t channels    = 0;
    uint64_t sampleCount = 0;
    if (!readRaw(is, sampleRate) || !readRaw(is, channels) || !readRaw(is, sampleCount)) {
        LOG_ERROR("Cooked sound '%s': truncated body", p.c_str());
        return false;
    }

    uint64_t remaining = payloadBytes - AUDIO_FIXED_BYTES;
    if (!takeCount(sampleCount, sizeof(int16_t), remaining, p, "sound", "sample")) return false;
    if (remaining != 0) {
        LOG_ERROR("Cooked sound '%s': payload size inconsistent with counts", p.c_str());
        return false;
    }

    if (channels == 0 || channels > MAX_AUDIO_CHANNELS) {
        LOG_ERROR(
            "Cooked sound '%s': %u channels is outside the 1..%u the format admits",
            p.c_str(),
            channels,
            MAX_AUDIO_CHANNELS
        );
        return false;
    }
    if (sampleRate == 0 || sampleRate > MAX_AUDIO_SAMPLE_RATE) {
        LOG_ERROR("Cooked sound '%s': implausible sample rate %u", p.c_str(), sampleRate);
        return false;
    }
    // Checked as well as sized: the right byte count can still describe frames that do not fit it, and
    // the mixer reads whole frames.
    if (sampleCount % channels != 0) {
        LOG_ERROR(
            "Cooked sound '%s': %llu samples do not divide into %u channels",
            p.c_str(),
            static_cast<unsigned long long>(sampleCount),
            channels
        );
        return false;
    }

    std::vector<int16_t> samples;
    readBulk(is, samples, sampleCount);
    if (!is) {
        LOG_ERROR("Cooked sound '%s': body read failed", p.c_str());
        return false;
    }

    out.sampleRate = sampleRate;
    out.channels   = channels;
    out.samples    = ClipSamples(std::move(samples));
    return true;
}

} // namespace Vkm::Engine::AssetCook
