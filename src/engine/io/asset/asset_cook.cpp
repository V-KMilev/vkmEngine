#define VKM_LOG_CATEGORY "IO"

#include "io/asset/asset_cook.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <type_traits>
#include <vector>

#include "logger.h"

#include "io/asset/asset_library.h"
#include "resource/asset/animation_clip_asset.h"
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

// The header is read/written field-by-field (never as a struct) so compiler
// padding can't leak into the format: magic[4] + sentinel + kind + version +
// recipeHash + payloadBytes.
constexpr std::streamoff HEADER_BYTES = 4 + sizeof(uint32_t) + sizeof(uint16_t) * 2 + sizeof(uint64_t) * 2;

// Mesh body: boundsMin + boundsMax + vertexCount + indexCount + skinCount +
// skinRadius + skeletonNameLen, then bulk vertices, indices, skin and the name.
constexpr uint64_t MESH_FIXED_BYTES = sizeof(glm::vec3) * 2 + sizeof(uint64_t) * 3
                                    + sizeof(float) + sizeof(uint32_t);
// Texture body: 2*u32 params + 7 enum bytes + 2 flag bytes + pixelBytes field.
constexpr uint64_t TEXTURE_FIXED_BYTES = sizeof(uint32_t) * 2 + 7 + 2 + sizeof(uint64_t);
// Skeleton body: boneCount, then per bone a {parent, nameLen} record, an
// inverse-bind matrix and a bind-pose TRS, then the concatenated name bytes.
constexpr uint64_t SKELETON_FIXED_BYTES    = sizeof(uint64_t);
constexpr uint64_t SKELETON_PER_BONE_BYTES = sizeof(int32_t) + sizeof(uint32_t)
                                           + sizeof(glm::mat4) + sizeof(Transform);
// Clip body: boneCount + duration + the six key-array counts + skeletonNameLen,
// then the bulk ClipBone table, the six key arrays, and the skeleton name.
constexpr uint64_t CLIP_FIXED_BYTES = sizeof(uint64_t) + sizeof(float)
                                    + sizeof(uint64_t) * 6 + sizeof(uint32_t);

// Bytes one texel occupies in the source pixel data. An out-of-range enum (this
// comes off disk) falls back to the same reading the backend gives it - RGBA /
// unsigned byte, see gl_format_conversion.h - so the size checked here is the
// size the upload will actually read.
uint64_t bytesPerTexel(TexturePixelFormat format, TexturePixelType type) {
    uint64_t channels = 4;
    switch (format) {
        case TexturePixelFormat::R:    channels = 1; break;
        case TexturePixelFormat::RG:   channels = 2; break;
        case TexturePixelFormat::RGB:  channels = 3; break;
        case TexturePixelFormat::RGBA: channels = 4; break;
    }
    uint64_t componentBytes = 1;
    switch (type) {
        case TexturePixelType::UnsignedByte: componentBytes = 1; break;
        case TexturePixelType::HalfFloat:    componentBytes = 2; break;
        case TexturePixelType::Float:        componentBytes = 4; break;
    }
    return channels * componentBytes;
}

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

// Bound one count by division against what is left of the payload, before it is
// multiplied by anything, and consume the bytes it claims. The size math then
// cannot wrap and resize() cannot be handed a bogus huge count off a corrupt or
// truncated file.
bool takeCount(uint64_t count, uint64_t elementBytes, uint64_t& remaining,
               const std::string& path, const char* what, const char* field) {
    if (count > remaining / elementBytes) {
        LOG_ERROR("Cooked %s '%s': implausible %s count %llu", what, path.c_str(), field,
                  static_cast<unsigned long long>(count));
        return false;
    }
    remaining -= count * elementBytes;
    return true;
}

// A clip channel addresses [first, first + count) of an array of `size` keys.
// Subtraction rather than addition so the bound cannot wrap.
bool channelInRange(const ClipChannel& channel, size_t size) {
    return channel.count <= size && channel.first <= size - channel.count;
}

// Bulk transfer of a trivially-copyable array. The counted arrays in the
// skeleton and clip bodies are all of this shape, and going through one pair
// keeps the reinterpret_cast in one place instead of a dozen.
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

void writeHeader(std::ostream& os, uint16_t assetKind, uint16_t formatVersion,
                 uint64_t recipeHash, uint64_t payloadBytes) {
    os.write(MAGIC, 4);
    writeRaw(os, ENDIAN_SENTINEL);
    writeRaw(os, assetKind);
    writeRaw(os, formatVersion);
    writeRaw(os, recipeHash);
    writeRaw(os, payloadBytes);
}

/**
 * @brief The header as written, before anything has judged it.
 */
struct CookedHeader {
    uint32_t sentinel      = 0;
    uint16_t assetKind     = 0;
    uint16_t formatVersion = 0;
    uint64_t recipeHash    = 0;
    uint64_t payloadBytes  = 0;
};

// How far the header got before it stopped making sense. The kind, the version
// and the hash are deliberately not judged here: a reader refuses a mismatch and
// says why, while isCookedCurrent only wants a yes or a no and must stay silent
// doing it, so the two disagree about everything except how the bytes are laid
// out - which is the only thing worth sharing.
enum class HeaderRead { Ok, BadMagic, ForeignEndian, Truncated };

HeaderRead readHeaderFields(std::istream& is, CookedHeader& out) {
    char magic[4] = {};
    if (!is.read(magic, 4) || std::memcmp(magic, MAGIC, 4) != 0) return HeaderRead::BadMagic;
    if (!readRaw(is, out.sentinel))                              return HeaderRead::Truncated;
    if (out.sentinel != ENDIAN_SENTINEL)                         return HeaderRead::ForeignEndian;
    if (!readRaw(is, out.assetKind) || !readRaw(is, out.formatVersion) ||
        !readRaw(is, out.recipeHash) || !readRaw(is, out.payloadBytes)) return HeaderRead::Truncated;
    return HeaderRead::Ok;
}

// The kind tag and format version a type's cooked file must carry. False for a
// material, which has no cooked binary at all - its recipe is its runtime form.
bool cookedIdentity(AssetType type, uint16_t& outKind, uint16_t& outVersion) {
    switch (type) {
        case AssetType::Mesh:          outKind = KIND_MESH;     outVersion = MESH_FORMAT_VERSION;           return true;
        case AssetType::Texture:       outKind = KIND_TEXTURE;  outVersion = TEXTURE_FORMAT_VERSION;        return true;
        case AssetType::Skeleton:      outKind = KIND_SKELETON; outVersion = SKELETON_FORMAT_VERSION;       return true;
        case AssetType::AnimationClip: outKind = KIND_CLIP;     outVersion = ANIMATION_CLIP_FORMAT_VERSION; return true;
        case AssetType::Material:
        case AssetType::Count:         return false;
    }
    return false;
}

// Reads and validates the header, leaving the get pointer at the body start.
bool readHeader(std::istream& is, const std::filesystem::path& path,
                uint16_t expectKind, uint16_t expectVersion,
                uint64_t& outRecipeHash, uint64_t& outPayloadBytes) {
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
        LOG_ERROR("Cooked asset '%s': wrong asset kind %u (expected %u)", p.c_str(),
                  header.assetKind, expectKind);
        return false;
    }
    if (header.formatVersion != expectVersion) {
        LOG_ERROR("Cooked asset '%s': unsupported format version %u (expected %u)", p.c_str(),
                  header.formatVersion, expectVersion);
        return false;
    }
    outRecipeHash   = header.recipeHash;
    outPayloadBytes = header.payloadBytes;
    return true;
}

// Whether the bytes after the header are exactly the payload the header
// declares. Says nothing and leaves the get pointer at the end, so both the
// reader (which reports the mismatch and then reads the body) and the staleness
// probe (which only wants a yes or a no) can ask it.
//
// The file is measured against the payload by subtraction, never by adding the
// payload to the header. payloadBytes is read off disk before anything has
// vouched for it, and a count near the top of the range overflows a signed file
// offset when the header is added to it - undefined behaviour on the one path
// whose whole job is to refuse a damaged file. Both operands below are known
// non-negative before they meet.
bool payloadFillsFile(std::istream& is, uint64_t payloadBytes, std::streamoff& outFileSize) {
    is.seekg(0, std::ios::end);
    outFileSize = is.tellg();
    return outFileSize >= HEADER_BYTES
        && payloadBytes == static_cast<uint64_t>(outFileSize - HEADER_BYTES);
}

// The bytes after the header must exactly equal the declared payload, so a
// corrupt count can never drive an oversized allocation. Repositions the get
// pointer to the body start.
bool verifyFileSize(std::istream& is, const std::filesystem::path& path, uint64_t payloadBytes) {
    std::streamoff fileSize = 0;
    const bool fills = payloadFillsFile(is, payloadBytes, fileSize);
    is.seekg(HEADER_BYTES, std::ios::beg);
    if (!fills) {
        LOG_ERROR("Cooked asset '%s': size mismatch (file %lld, header %lld + payload %llu)",
                  path.string().c_str(), static_cast<long long>(fileSize),
                  static_cast<long long>(HEADER_BYTES),
                  static_cast<unsigned long long>(payloadBytes));
        return false;
    }
    return true;
}

// Create parent dirs and open `path` (truncating) for a cooked write. The
// returned stream is unopened on failure - check `if (!os)` at the call site.
std::ofstream openCookedWrite(const std::filesystem::path& path, const char* what) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) LOG_ERROR("Cooked %s '%s': cannot open for writing", what, path.string().c_str());
    return os;
}

// Open `path` and validate its header + declared size against the expected
// kind/version/fixed-body size. On success `is` is positioned at the body start
// and outRecipeHash / outPayloadBytes are filled.
bool openCookedRead(std::ifstream& is, const std::filesystem::path& path,
                    uint16_t expectKind, uint16_t expectVersion, uint64_t fixedBytes,
                    const char* what, uint64_t& outRecipeHash, uint64_t& outPayloadBytes) {
    is.open(path, std::ios::binary);
    if (!is) {
        LOG_ERROR("Cooked %s '%s': cannot open", what, path.string().c_str());
        return false;
    }
    if (!readHeader(is, path, expectKind, expectVersion, outRecipeHash, outPayloadBytes)) return false;
    if (!verifyFileSize(is, path, outPayloadBytes)) return false;
    if (outPayloadBytes < fixedBytes) {
        LOG_ERROR("Cooked %s '%s': payload too small", what, path.string().c_str());
        return false;
    }
    return true;
}

} // namespace

bool writeMesh(const std::filesystem::path& path, const MeshAsset& mesh, uint64_t recipeHash) {
    static_assert(sizeof(Vertex) == 48, "Vertex layout changed - bump MESH_FORMAT_VERSION and the cook format");
    static_assert(std::is_trivially_copyable_v<Vertex>, "Vertex must be trivially copyable to bulk-write");

    const uint64_t vertexCount = mesh.vertices.size();
    const uint64_t indexCount  = mesh.indices.size();
    const uint64_t skinCount   = mesh.skin.size();
    // The skin stream is parallel to the vertices or absent; there is no third
    // state, and the vertex stage reads them by the same index.
    if (skinCount != 0 && skinCount != vertexCount) {
        LOG_ERROR("Cooked mesh '%s': %llu skin entries against %llu vertices",
                  path.string().c_str(), static_cast<unsigned long long>(skinCount),
                  static_cast<unsigned long long>(vertexCount));
        return false;
    }

    std::ofstream os = openCookedWrite(path, "mesh");
    if (!os) return false;

    const uint64_t payloadBytes = MESH_FIXED_BYTES
        + vertexCount * sizeof(Vertex) + indexCount * sizeof(uint32_t)
        + skinCount * sizeof(SkinVertex) + mesh.skeleton.size();

    writeHeader(os, KIND_MESH, MESH_FORMAT_VERSION, recipeHash, payloadBytes);
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
    return true;
}

bool isCookedCurrent(AssetType type, const std::filesystem::path& path, uint64_t recipeHash) {
    uint16_t expectKind = 0;
    uint16_t expectVersion = 0;
    if (!cookedIdentity(type, expectKind, expectVersion)) return false;

    std::ifstream is(path, std::ios::binary);
    if (!is) return false;

    CookedHeader header;
    if (readHeaderFields(is, header) != HeaderRead::Ok) return false;
    if (header.assetKind != expectKind || header.formatVersion != expectVersion
        || header.recipeHash != recipeHash) return false;

    // The body is measured, not read. The header is written before the body, so
    // an interrupted write or a partial copy leaves a file that identifies
    // itself perfectly and is short - and identifying itself is all the rest of
    // this function asks. Without this the reader refuses that file while the
    // cooker calls it current and never rewrites it, which is precisely the
    // project neither end repairs. One seek answers it; nothing is allocated.
    std::streamoff fileSize = 0;
    return payloadFillsFile(is, header.payloadBytes, fileSize);
}

bool readMesh(const std::filesystem::path& path, MeshAsset& out, uint64_t* outHash) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t recipeHash = 0;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, KIND_MESH, MESH_FORMAT_VERSION, MESH_FIXED_BYTES, "mesh",
                        recipeHash, payloadBytes)) return false;

    glm::vec3 boundsMin{0};
    glm::vec3 boundsMax{0};
    uint64_t vertexCount = 0;
    uint64_t indexCount  = 0;
    uint64_t skinCount   = 0;
    float    skinRadius  = 0.0f;
    uint32_t skeletonNameLen = 0;
    if (!readRaw(is, boundsMin) || !readRaw(is, boundsMax) ||
        !readRaw(is, vertexCount) || !readRaw(is, indexCount) ||
        !readRaw(is, skinCount) || !readRaw(is, skinRadius) || !readRaw(is, skeletonNameLen)) {
        LOG_ERROR("Cooked mesh '%s': truncated body", p.c_str());
        return false;
    }

    uint64_t remaining = payloadBytes - MESH_FIXED_BYTES;
    if (!takeCount(vertexCount,     sizeof(Vertex),     remaining, p, "mesh", "vertex")   ||
        !takeCount(indexCount,      sizeof(uint32_t),   remaining, p, "mesh", "index")    ||
        !takeCount(skinCount,       sizeof(SkinVertex), remaining, p, "mesh", "skin")     ||
        !takeCount(skeletonNameLen, 1,                  remaining, p, "mesh", "rig name")) return false;
    if (remaining != 0) {
        LOG_ERROR("Cooked mesh '%s': payload size inconsistent with counts", p.c_str());
        return false;
    }
    // Parallel or absent, checked here as well as at write: the vertex stage
    // reads both streams by the same index, so a short skin stream is an
    // out-of-bounds fetch on every draw.
    if (skinCount != 0 && skinCount != vertexCount) {
        LOG_ERROR("Cooked mesh '%s': %llu skin entries against %llu vertices", p.c_str(),
                  static_cast<unsigned long long>(skinCount), static_cast<unsigned long long>(vertexCount));
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
    // One check for the whole body: the reconciliation above already proved the
    // bytes are there, so a failure here is an IO error, and failbit is sticky.
    if (!is) {
        LOG_ERROR("Cooked mesh '%s': body read failed", p.c_str());
        out.vertices.clear();
        out.indices.clear();
        out.skin.clear();
        return false;
    }

    // Indices are bounds-checked as well as sized. A file can pass every size
    // check above and still name vertices that do not exist - a truncated write
    // resumed, a bad sector - and nothing downstream re-checks: decimation
    // indexes a per-vertex array with them, and GL is handed the buffer as-is.
    const auto vertexTotal = static_cast<uint32_t>(out.vertices.size());
    for (const uint32_t index : out.indices) {
        if (index >= vertexTotal) {
            LOG_ERROR("Cooked mesh '%s': index %u is past the %u vertices it declares",
                      p.c_str(), index, vertexTotal);
            out.vertices.clear();
            out.indices.clear();
            out.skin.clear();
            return false;
        }
    }

    // Bone indices get the same treatment, and for a sharper reason: they are
    // not read by the CPU at all, they address the pose palette in the vertex
    // stage. A corrupt one is an out-of-range buffer read on every vertex of
    // every frame, and the palette is the only thing that would notice.
    for (const SkinVertex& skin : out.skin) {
        for (const uint16_t bone : skin.bones) {
            if (bone >= MAX_SKELETON_BONES) {
                LOG_ERROR("Cooked mesh '%s': bone index %u is past the %u a rig can hold",
                          p.c_str(), bone, MAX_SKELETON_BONES);
                out.vertices.clear();
                out.indices.clear();
                out.skin.clear();
                return false;
            }
        }
    }

    if (outHash) *outHash = recipeHash;
    return true;
}

bool writeTexture(const std::filesystem::path& path, const TextureAsset& texture, uint64_t recipeHash) {
    // Every TextureParams enum below is written as its raw value, so the file
    // format IS the enumerator order. Reordering one is invisible to both the
    // version check and the recipe hash - a cooked texture would simply decode
    // as a different format, with no error. Every enumerator is spelled out
    // rather than only the last, or a swap in the middle leaves the tail where
    // it was and passes; appending stays legal. The message names the same
    // remedy as the Vertex guard in writeMesh.
    static_assert(static_cast<uint8_t>(TextureInternalFormat::R8)      == 0 &&
                  static_cast<uint8_t>(TextureInternalFormat::RG8)     == 1 &&
                  static_cast<uint8_t>(TextureInternalFormat::RGB8)    == 2 &&
                  static_cast<uint8_t>(TextureInternalFormat::RGBA8)   == 3 &&
                  static_cast<uint8_t>(TextureInternalFormat::SRGB8)   == 4 &&
                  static_cast<uint8_t>(TextureInternalFormat::SRGBA8)  == 5 &&
                  static_cast<uint8_t>(TextureInternalFormat::RGBA16F) == 6 &&
                  static_cast<uint8_t>(TextureInternalFormat::RGBA32F) == 7,
                  "TextureInternalFormat reordered - bump TEXTURE_FORMAT_VERSION");
    static_assert(static_cast<uint8_t>(TexturePixelFormat::R)    == 0 &&
                  static_cast<uint8_t>(TexturePixelFormat::RG)   == 1 &&
                  static_cast<uint8_t>(TexturePixelFormat::RGB)  == 2 &&
                  static_cast<uint8_t>(TexturePixelFormat::RGBA) == 3,
                  "TexturePixelFormat reordered - bump TEXTURE_FORMAT_VERSION");
    static_assert(static_cast<uint8_t>(TexturePixelType::UnsignedByte) == 0 &&
                  static_cast<uint8_t>(TexturePixelType::Float)        == 1 &&
                  static_cast<uint8_t>(TexturePixelType::HalfFloat)    == 2,
                  "TexturePixelType reordered - bump TEXTURE_FORMAT_VERSION");
    static_assert(static_cast<uint8_t>(TextureWrapMode::Repeat)         == 0 &&
                  static_cast<uint8_t>(TextureWrapMode::MirroredRepeat) == 1 &&
                  static_cast<uint8_t>(TextureWrapMode::ClampToEdge)    == 2 &&
                  static_cast<uint8_t>(TextureWrapMode::ClampToBorder)  == 3,
                  "TextureWrapMode reordered - bump TEXTURE_FORMAT_VERSION");
    static_assert(static_cast<uint8_t>(TextureMinFilter::Nearest)              == 0 &&
                  static_cast<uint8_t>(TextureMinFilter::Linear)               == 1 &&
                  static_cast<uint8_t>(TextureMinFilter::NearestMipmapNearest) == 2 &&
                  static_cast<uint8_t>(TextureMinFilter::LinearMipmapNearest)  == 3 &&
                  static_cast<uint8_t>(TextureMinFilter::NearestMipmapLinear)  == 4 &&
                  static_cast<uint8_t>(TextureMinFilter::LinearMipmapLinear)   == 5,
                  "TextureMinFilter reordered - bump TEXTURE_FORMAT_VERSION");
    static_assert(static_cast<uint8_t>(TextureMagFilter::Nearest) == 0 &&
                  static_cast<uint8_t>(TextureMagFilter::Linear)  == 1,
                  "TextureMagFilter reordered - bump TEXTURE_FORMAT_VERSION");

    std::ofstream os = openCookedWrite(path, "texture");
    if (!os) return false;

    const TextureParams& tp = texture.params;
    const uint64_t pixelBytes  = texture.pixelData.size();
    const uint64_t payloadBytes = TEXTURE_FIXED_BYTES + pixelBytes;

    writeHeader(os, KIND_TEXTURE, TEXTURE_FORMAT_VERSION, recipeHash, payloadBytes);
    writeRaw(os, tp.width);
    writeRaw(os, tp.height);
    writeRaw(os, tp.internalFormat);
    writeRaw(os, tp.format);
    writeRaw(os, tp.type);
    writeRaw(os, tp.wrapS);
    writeRaw(os, tp.wrapT);
    writeRaw(os, tp.minFilter);
    writeRaw(os, tp.magFilter);
    writeRaw(os, static_cast<uint8_t>(tp.generateMipmaps));
    writeRaw(os, static_cast<uint8_t>(texture.srgb));
    writeRaw(os, pixelBytes);
    if (pixelBytes) os.write(reinterpret_cast<const char*>(texture.pixelData.data()), pixelBytes);

    if (!os) {
        LOG_ERROR("Cooked texture '%s': write failed", path.string().c_str());
        return false;
    }
    return true;
}

bool readTexture(const std::filesystem::path& path, TextureAsset& out, uint64_t* outHash) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t recipeHash = 0;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, KIND_TEXTURE, TEXTURE_FORMAT_VERSION, TEXTURE_FIXED_BYTES, "texture",
                        recipeHash, payloadBytes)) return false;

    TextureParams tp;
    uint8_t generateMipmaps = 0;
    uint8_t srgb = 0;
    uint64_t pixelBytes = 0;
    if (!readRaw(is, tp.width) || !readRaw(is, tp.height) ||
        !readRaw(is, tp.internalFormat) || !readRaw(is, tp.format) || !readRaw(is, tp.type) ||
        !readRaw(is, tp.wrapS) || !readRaw(is, tp.wrapT) || !readRaw(is, tp.minFilter) || !readRaw(is, tp.magFilter) ||
        !readRaw(is, generateMipmaps) || !readRaw(is, srgb) || !readRaw(is, pixelBytes)) {
        LOG_ERROR("Cooked texture '%s': truncated body", p.c_str());
        return false;
    }
    tp.generateMipmaps = (generateMipmaps != 0);

    if (pixelBytes != payloadBytes - TEXTURE_FIXED_BYTES) {
        LOG_ERROR("Cooked texture '%s': pixel size inconsistent with payload", p.c_str());
        return false;
    }

    // Dimensions are checked as well as sized, for the same reason mesh indices
    // are: a file can carry the right number of bytes and still declare a size
    // that does not describe them, and nothing downstream re-checks - the params
    // reach glTexImage2D verbatim, which then reads width * height texels out of
    // this buffer. Division rather than multiplication so the math cannot wrap.
    const uint64_t texelBytes = bytesPerTexel(tp.format, tp.type);
    const uint64_t texels     = static_cast<uint64_t>(tp.width) * tp.height;
    if (pixelBytes % texelBytes != 0 || pixelBytes / texelBytes != texels) {
        LOG_ERROR("Cooked texture '%s': %llu pixel byte(s) do not describe %ux%u texels",
                  p.c_str(), static_cast<unsigned long long>(pixelBytes), tp.width, tp.height);
        return false;
    }

    out.params = tp;
    out.srgb   = (srgb != 0);
    out.pixelData.resize(static_cast<size_t>(pixelBytes));
    if (pixelBytes && !is.read(reinterpret_cast<char*>(out.pixelData.data()), pixelBytes)) {
        LOG_ERROR("Cooked texture '%s': pixel read failed", p.c_str());
        return false;
    }

    if (outHash) *outHash = recipeHash;
    return true;
}

bool writeSkeleton(const std::filesystem::path& path, const SkeletonAsset& skeleton, uint64_t recipeHash) {
    static_assert(sizeof(Transform) == 40,
                  "Transform layout changed - bump SKELETON_FORMAT_VERSION and the cook format");
    static_assert(std::is_trivially_copyable_v<Transform>, "Transform must be trivially copyable to bulk-write");

    const uint64_t boneCount = skeleton.bones.size();
    // The three arrays are the asset's invariant, and the bulk writes below read
    // boneCount elements out of each; a caller that broke it would otherwise
    // hand us an out-of-bounds read rather than a rejected cook.
    if (skeleton.inverseBind.size() != boneCount || skeleton.bindPose.size() != boneCount) {
        LOG_ERROR("Cooked skeleton '%s': %llu bone(s) against %zu inverse-bind and %zu bind-pose entries",
                  path.string().c_str(), static_cast<unsigned long long>(boneCount),
                  skeleton.inverseBind.size(), skeleton.bindPose.size());
        return false;
    }
    if (boneCount > MAX_SKELETON_BONES) {
        LOG_ERROR("Cooked skeleton '%s': %llu bones is past the %u the format admits",
                  path.string().c_str(), static_cast<unsigned long long>(boneCount), MAX_SKELETON_BONES);
        return false;
    }

    std::ofstream os = openCookedWrite(path, "skeleton");
    if (!os) return false;

    uint64_t nameBytes = 0;
    for (const Bone& bone : skeleton.bones) nameBytes += bone.name.size();
    const uint64_t payloadBytes = SKELETON_FIXED_BYTES + boneCount * SKELETON_PER_BONE_BYTES + nameBytes;

    writeHeader(os, KIND_SKELETON, SKELETON_FORMAT_VERSION, recipeHash, payloadBytes);
    writeRaw(os, boneCount);
    for (const Bone& bone : skeleton.bones) {
        writeRaw(os, bone.parent);
        writeRaw(os, static_cast<uint32_t>(bone.name.size()));
    }
    writeBulk(os, skeleton.inverseBind);
    writeBulk(os, skeleton.bindPose);
    for (const Bone& bone : skeleton.bones) os.write(bone.name.data(), static_cast<std::streamsize>(bone.name.size()));

    if (!os) {
        LOG_ERROR("Cooked skeleton '%s': write failed", path.string().c_str());
        return false;
    }
    return true;
}

bool readSkeleton(const std::filesystem::path& path, SkeletonAsset& out, uint64_t* outHash) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t recipeHash = 0;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, KIND_SKELETON, SKELETON_FORMAT_VERSION, SKELETON_FIXED_BYTES, "skeleton",
                        recipeHash, payloadBytes)) return false;

    uint64_t boneCount = 0;
    if (!readRaw(is, boneCount)) {
        LOG_ERROR("Cooked skeleton '%s': truncated body", p.c_str());
        return false;
    }

    // Division rather than multiplication, so the size math cannot wrap and a
    // corrupt count cannot drive an oversized resize.
    const uint64_t afterFixed = payloadBytes - SKELETON_FIXED_BYTES;
    if (boneCount > afterFixed / SKELETON_PER_BONE_BYTES) {
        LOG_ERROR("Cooked skeleton '%s': implausible bone count %llu", p.c_str(),
                  static_cast<unsigned long long>(boneCount));
        return false;
    }
    if (boneCount > MAX_SKELETON_BONES) {
        LOG_ERROR("Cooked skeleton '%s': %llu bones is past the %u the format admits", p.c_str(),
                  static_cast<unsigned long long>(boneCount), MAX_SKELETON_BONES);
        return false;
    }

    // What is left once the fixed-size records are accounted for is exactly the
    // name blob, so each name length is bounded against the remainder of it.
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
        // The ordering invariant, re-checked rather than assumed. It is strictly
        // stronger than a range check and it is what lets every consumer compose
        // the pose in one forward loop: a bone that named a later parent - or
        // itself - would leave the loop reading a transform it has not written.
        if (parent < -1 || parent >= static_cast<int32_t>(i)) {
            LOG_ERROR("Cooked skeleton '%s': bone %llu names parent %d, which is not a bone before it",
                      p.c_str(), static_cast<unsigned long long>(i), parent);
            out.bones.clear();
            return false;
        }
        if (nameLen > nameBudget - nameBytes) {
            LOG_ERROR("Cooked skeleton '%s': bone %llu declares a %u-byte name past the payload",
                      p.c_str(), static_cast<unsigned long long>(i), nameLen);
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
    // One check for the whole body: the size reconciliation above already proved
    // the bytes are there, so a failure here is an IO error, and the stream's
    // failbit is sticky once one read misses.
    if (!is) {
        LOG_ERROR("Cooked skeleton '%s': body read failed", p.c_str());
        out.bones.clear();
        out.inverseBind.clear();
        out.bindPose.clear();
        return false;
    }

    if (outHash) *outHash = recipeHash;
    return true;
}

bool writeAnimationClip(const std::filesystem::path& path, const AnimationClipAsset& clip, uint64_t recipeHash) {
    static_assert(sizeof(ClipBone) == 24,
                  "ClipBone layout changed - bump ANIMATION_CLIP_FORMAT_VERSION and the cook format");
    static_assert(std::is_trivially_copyable_v<ClipBone>, "ClipBone must be trivially copyable to bulk-write");

    const uint64_t boneCount = clip.bones.size();
    if (boneCount > MAX_SKELETON_BONES) {
        LOG_ERROR("Cooked clip '%s': %llu bones is past the %u the format admits",
                  path.string().c_str(), static_cast<unsigned long long>(boneCount), MAX_SKELETON_BONES);
        return false;
    }
    if (clip.positionTimes.size() != clip.positions.size() ||
        clip.rotationTimes.size() != clip.rotations.size() ||
        clip.scaleTimes.size()    != clip.scales.size()) {
        LOG_ERROR("Cooked clip '%s': key times and values disagree in length", path.string().c_str());
        return false;
    }
    for (uint64_t i = 0; i < boneCount; ++i) {
        const ClipBone& bone = clip.bones[static_cast<size_t>(i)];
        if (!channelInRange(bone.position, clip.positions.size()) ||
            !channelInRange(bone.rotation, clip.rotations.size()) ||
            !channelInRange(bone.scale,    clip.scales.size())) {
            LOG_ERROR("Cooked clip '%s': bone %llu names keys it does not have",
                      path.string().c_str(), static_cast<unsigned long long>(i));
            return false;
        }
    }

    std::ofstream os = openCookedWrite(path, "clip");
    if (!os) return false;

    const uint64_t payloadBytes = CLIP_FIXED_BYTES
        + boneCount * sizeof(ClipBone)
        + clip.positionTimes.size() * sizeof(float)     + clip.positions.size() * sizeof(glm::vec3)
        + clip.rotationTimes.size() * sizeof(float)     + clip.rotations.size() * sizeof(glm::quat)
        + clip.scaleTimes.size()    * sizeof(float)     + clip.scales.size()    * sizeof(glm::vec3)
        + clip.skeleton.size();

    writeHeader(os, KIND_CLIP, ANIMATION_CLIP_FORMAT_VERSION, recipeHash, payloadBytes);
    writeRaw(os, boneCount);
    writeRaw(os, clip.duration);
    writeRaw(os, static_cast<uint64_t>(clip.positionTimes.size()));
    writeRaw(os, static_cast<uint64_t>(clip.positions.size()));
    writeRaw(os, static_cast<uint64_t>(clip.rotationTimes.size()));
    writeRaw(os, static_cast<uint64_t>(clip.rotations.size()));
    writeRaw(os, static_cast<uint64_t>(clip.scaleTimes.size()));
    writeRaw(os, static_cast<uint64_t>(clip.scales.size()));
    writeRaw(os, static_cast<uint32_t>(clip.skeleton.size()));
    writeBulk(os, clip.bones);
    writeBulk(os, clip.positionTimes);
    writeBulk(os, clip.positions);
    writeBulk(os, clip.rotationTimes);
    writeBulk(os, clip.rotations);
    writeBulk(os, clip.scaleTimes);
    writeBulk(os, clip.scales);
    os.write(clip.skeleton.data(), static_cast<std::streamsize>(clip.skeleton.size()));

    if (!os) {
        LOG_ERROR("Cooked clip '%s': write failed", path.string().c_str());
        return false;
    }
    return true;
}

bool readAnimationClip(const std::filesystem::path& path, AnimationClipAsset& out, uint64_t* outHash) {
    const std::string p = path.string();
    std::ifstream is;
    uint64_t recipeHash = 0;
    uint64_t payloadBytes = 0;
    if (!openCookedRead(is, path, KIND_CLIP, ANIMATION_CLIP_FORMAT_VERSION, CLIP_FIXED_BYTES, "clip",
                        recipeHash, payloadBytes)) return false;

    uint64_t boneCount = 0;
    float    duration  = 0.0f;
    uint64_t positionTimeCount = 0, positionCount = 0;
    uint64_t rotationTimeCount = 0, rotationCount = 0;
    uint64_t scaleTimeCount    = 0, scaleCount    = 0;
    uint32_t skeletonNameLen   = 0;
    if (!readRaw(is, boneCount) || !readRaw(is, duration) ||
        !readRaw(is, positionTimeCount) || !readRaw(is, positionCount) ||
        !readRaw(is, rotationTimeCount) || !readRaw(is, rotationCount) ||
        !readRaw(is, scaleTimeCount)    || !readRaw(is, scaleCount) ||
        !readRaw(is, skeletonNameLen)) {
        LOG_ERROR("Cooked clip '%s': truncated body", p.c_str());
        return false;
    }

    // Every count is bounded in the order the arrays are written.
    uint64_t remaining = payloadBytes - CLIP_FIXED_BYTES;
    if (!takeCount(boneCount,         sizeof(ClipBone),  remaining, p, "clip", "bone")          ||
        !takeCount(positionTimeCount, sizeof(float),     remaining, p, "clip", "position time") ||
        !takeCount(positionCount,     sizeof(glm::vec3), remaining, p, "clip", "position")      ||
        !takeCount(rotationTimeCount, sizeof(float),     remaining, p, "clip", "rotation time") ||
        !takeCount(rotationCount,     sizeof(glm::quat), remaining, p, "clip", "rotation")      ||
        !takeCount(scaleTimeCount,    sizeof(float),     remaining, p, "clip", "scale time")    ||
        !takeCount(scaleCount,        sizeof(glm::vec3), remaining, p, "clip", "scale")         ||
        !takeCount(skeletonNameLen,   1,                 remaining, p, "clip", "rig name")) return false;
    if (remaining != 0) {
        LOG_ERROR("Cooked clip '%s': payload size inconsistent with counts", p.c_str());
        return false;
    }

    if (boneCount > MAX_SKELETON_BONES) {
        LOG_ERROR("Cooked clip '%s': %llu bones is past the %u the format admits", p.c_str(),
                  static_cast<unsigned long long>(boneCount), MAX_SKELETON_BONES);
        return false;
    }
    // Times and values are read as one key each, and the sampler walks them in
    // lockstep; a file where they disagree would sample a value that is not
    // there. Checked separately from the size reconciliation, which two
    // compensating corruptions could still satisfy.
    if (positionTimeCount != positionCount || rotationTimeCount != rotationCount ||
        scaleTimeCount != scaleCount) {
        LOG_ERROR("Cooked clip '%s': key times and values disagree in length", p.c_str());
        return false;
    }
    // The sampler divides by the duration to wrap a looping clip, and every
    // consumer clamps against it; a NaN out of a damaged file would spread
    // through the pose rather than stopping here.
    if (!std::isfinite(duration) || duration < 0.0f) {
        LOG_ERROR("Cooked clip '%s': implausible duration %f", p.c_str(), static_cast<double>(duration));
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
    if (!is) {
        LOG_ERROR("Cooked clip '%s': body read failed", p.c_str());
        out.bones.clear();
        return false;
    }

    // Ranges are bounds-checked as well as sized, for the same reason mesh
    // indices are: a file can carry the right number of bytes and still name
    // keys that are not in it, and nothing downstream re-checks - the sampler
    // indexes these arrays directly, once per bone per frame.
    for (size_t i = 0; i < out.bones.size(); ++i) {
        const ClipBone& bone = out.bones[i];
        if (!channelInRange(bone.position, out.positions.size()) ||
            !channelInRange(bone.rotation, out.rotations.size()) ||
            !channelInRange(bone.scale,    out.scales.size())) {
            LOG_ERROR("Cooked clip '%s': bone %zu names keys past the %zu/%zu/%zu it declares",
                      p.c_str(), i, out.positions.size(), out.rotations.size(), out.scales.size());
            out.bones.clear();
            return false;
        }
    }

    out.duration = duration;
    if (outHash) *outHash = recipeHash;
    return true;
}

} // namespace Vkm::Engine::AssetCook
