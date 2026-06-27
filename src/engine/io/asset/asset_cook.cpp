#define VKM_LOG_CATEGORY "IO"

#include "io/asset/asset_cook.h"

#include <cstring>
#include <fstream>
#include <type_traits>

#include "logger.h"

#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"

namespace Engine::AssetCook {

namespace {

constexpr char     MAGIC[4]        = {'V', 'K', 'M', 'C'};
constexpr uint32_t ENDIAN_SENTINEL = 0x01020304u;
constexpr uint16_t KIND_MESH       = 1;
constexpr uint16_t KIND_TEXTURE    = 2;

// The header is read/written field-by-field (never as a struct) so compiler
// padding can't leak into the format: magic[4] + sentinel + kind + version +
// recipeHash + payloadBytes.
constexpr std::streamoff HEADER_BYTES = 4 + sizeof(uint32_t) + sizeof(uint16_t) * 2 + sizeof(uint64_t) * 2;

// Mesh body: boundsMin + boundsMax + vertexCount + indexCount, then bulk data.
constexpr uint64_t MESH_FIXED_BYTES = sizeof(glm::vec3) * 2 + sizeof(uint64_t) * 2;
// Texture body: 2*u32 params + 7 enum bytes + 2 flag bytes + pixelBytes field.
constexpr uint64_t TEXTURE_FIXED_BYTES = sizeof(uint32_t) * 2 + 7 + 2 + sizeof(uint64_t);

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

void writeHeader(std::ostream& os, uint16_t assetKind, uint16_t formatVersion,
                 uint64_t recipeHash, uint64_t payloadBytes) {
    os.write(MAGIC, 4);
    writeRaw(os, ENDIAN_SENTINEL);
    writeRaw(os, assetKind);
    writeRaw(os, formatVersion);
    writeRaw(os, recipeHash);
    writeRaw(os, payloadBytes);
}

// Reads and validates the header, leaving the get pointer at the body start.
bool readHeader(std::istream& is, const std::filesystem::path& path,
                uint16_t expectKind, uint16_t expectVersion,
                uint64_t& outRecipeHash, uint64_t& outPayloadBytes) {
    const std::string p = path.string();
    char magic[4] = {};
    if (!is.read(magic, 4) || std::memcmp(magic, MAGIC, 4) != 0) {
        LOG_ERROR("Cooked asset '%s': bad magic", p.c_str());
        return false;
    }
    uint32_t sentinel = 0;
    if (!readRaw(is, sentinel) || sentinel != ENDIAN_SENTINEL) {
        LOG_ERROR("Cooked asset '%s': endian/sentinel mismatch (0x%08x)", p.c_str(), sentinel);
        return false;
    }
    uint16_t kind = 0;
    uint16_t version = 0;
    if (!readRaw(is, kind) || kind != expectKind) {
        LOG_ERROR("Cooked asset '%s': wrong asset kind %u (expected %u)", p.c_str(), kind, expectKind);
        return false;
    }
    if (!readRaw(is, version) || version != expectVersion) {
        LOG_ERROR("Cooked asset '%s': unsupported format version %u (expected %u)", p.c_str(), version, expectVersion);
        return false;
    }
    if (!readRaw(is, outRecipeHash) || !readRaw(is, outPayloadBytes)) {
        LOG_ERROR("Cooked asset '%s': truncated header", p.c_str());
        return false;
    }
    return true;
}

// The bytes after the header must exactly equal the declared payload, so a
// corrupt count can never drive an oversized allocation. Repositions the get
// pointer to the body start.
bool verifyFileSize(std::istream& is, const std::filesystem::path& path, uint64_t payloadBytes) {
    is.seekg(0, std::ios::end);
    const std::streamoff fileSize = is.tellg();
    is.seekg(HEADER_BYTES, std::ios::beg);
    const std::streamoff expected = HEADER_BYTES + static_cast<std::streamoff>(payloadBytes);
    if (fileSize < HEADER_BYTES || fileSize != expected) {
        LOG_ERROR("Cooked asset '%s': size mismatch (file %lld, header+payload %lld)",
                  path.string().c_str(), static_cast<long long>(fileSize), static_cast<long long>(expected));
        return false;
    }
    return true;
}

} // namespace

bool writeMesh(const std::filesystem::path& path, const MeshAsset& mesh, uint64_t recipeHash) {
    static_assert(sizeof(Vertex) == 48, "Vertex layout changed - bump MESH_FORMAT_VERSION and the cook format");
    static_assert(std::is_trivially_copyable_v<Vertex>, "Vertex must be trivially copyable to bulk-write");

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) {
        LOG_ERROR("Cooked mesh '%s': cannot open for writing", path.string().c_str());
        return false;
    }

    const uint64_t vertexCount = mesh.vertices.size();
    const uint64_t indexCount  = mesh.indices.size();
    const uint64_t payloadBytes =
        MESH_FIXED_BYTES + vertexCount * sizeof(Vertex) + indexCount * sizeof(uint32_t);

    writeHeader(os, KIND_MESH, MESH_FORMAT_VERSION, recipeHash, payloadBytes);
    writeRaw(os, mesh.boundsMin);
    writeRaw(os, mesh.boundsMax);
    writeRaw(os, vertexCount);
    writeRaw(os, indexCount);
    if (vertexCount) os.write(reinterpret_cast<const char*>(mesh.vertices.data()), vertexCount * sizeof(Vertex));
    if (indexCount)  os.write(reinterpret_cast<const char*>(mesh.indices.data()),  indexCount * sizeof(uint32_t));

    if (!os) {
        LOG_ERROR("Cooked mesh '%s': write failed", path.string().c_str());
        return false;
    }
    return true;
}

bool readMesh(const std::filesystem::path& path, MeshAsset& out, uint64_t* outHash) {
    const std::string p = path.string();
    std::ifstream is(path, std::ios::binary);
    if (!is) {
        LOG_ERROR("Cooked mesh '%s': cannot open", p.c_str());
        return false;
    }

    uint64_t recipeHash = 0;
    uint64_t payloadBytes = 0;
    if (!readHeader(is, path, KIND_MESH, MESH_FORMAT_VERSION, recipeHash, payloadBytes)) return false;
    if (!verifyFileSize(is, path, payloadBytes)) return false;
    if (payloadBytes < MESH_FIXED_BYTES) {
        LOG_ERROR("Cooked mesh '%s': payload too small", p.c_str());
        return false;
    }

    glm::vec3 boundsMin{0};
    glm::vec3 boundsMax{0};
    uint64_t vertexCount = 0;
    uint64_t indexCount  = 0;
    if (!readRaw(is, boundsMin) || !readRaw(is, boundsMax) ||
        !readRaw(is, vertexCount) || !readRaw(is, indexCount)) {
        LOG_ERROR("Cooked mesh '%s': truncated body", p.c_str());
        return false;
    }

    // Bound each count against the remaining payload before multiplying, so the
    // size math can't overflow and resize() can't be handed a bogus huge count.
    const uint64_t afterFixed = payloadBytes - MESH_FIXED_BYTES;
    if (vertexCount > afterFixed / sizeof(Vertex)) {
        LOG_ERROR("Cooked mesh '%s': implausible vertex count %llu", p.c_str(), static_cast<unsigned long long>(vertexCount));
        return false;
    }
    const uint64_t afterVerts = afterFixed - vertexCount * sizeof(Vertex);
    if (indexCount > afterVerts / sizeof(uint32_t)) {
        LOG_ERROR("Cooked mesh '%s': implausible index count %llu", p.c_str(), static_cast<unsigned long long>(indexCount));
        return false;
    }
    if (vertexCount * sizeof(Vertex) + indexCount * sizeof(uint32_t) != afterFixed) {
        LOG_ERROR("Cooked mesh '%s': payload size inconsistent with counts", p.c_str());
        return false;
    }

    out.boundsMin = boundsMin;
    out.boundsMax = boundsMax;
    out.vertices.resize(static_cast<size_t>(vertexCount));
    out.indices.resize(static_cast<size_t>(indexCount));
    if (vertexCount && !is.read(reinterpret_cast<char*>(out.vertices.data()), vertexCount * sizeof(Vertex))) {
        LOG_ERROR("Cooked mesh '%s': vertex read failed", p.c_str());
        return false;
    }
    if (indexCount && !is.read(reinterpret_cast<char*>(out.indices.data()), indexCount * sizeof(uint32_t))) {
        LOG_ERROR("Cooked mesh '%s': index read failed", p.c_str());
        return false;
    }

    if (outHash) *outHash = recipeHash;
    return true;
}

bool writeTexture(const std::filesystem::path& path, const TextureAsset& texture, uint64_t recipeHash) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) {
        LOG_ERROR("Cooked texture '%s': cannot open for writing", path.string().c_str());
        return false;
    }

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
    std::ifstream is(path, std::ios::binary);
    if (!is) {
        LOG_ERROR("Cooked texture '%s': cannot open", p.c_str());
        return false;
    }

    uint64_t recipeHash = 0;
    uint64_t payloadBytes = 0;
    if (!readHeader(is, path, KIND_TEXTURE, TEXTURE_FORMAT_VERSION, recipeHash, payloadBytes)) return false;
    if (!verifyFileSize(is, path, payloadBytes)) return false;
    if (payloadBytes < TEXTURE_FIXED_BYTES) {
        LOG_ERROR("Cooked texture '%s': payload too small", p.c_str());
        return false;
    }

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

} // namespace Engine::AssetCook
