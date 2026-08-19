#pragma once

#include <cstdint>
#include <filesystem>

namespace Vkm::Engine {

struct MeshAsset;
struct TextureAsset;

/**
 * @brief Cooked binary asset format (derived cache).
 *
 * A cooked file is produced by the editor cooker from an asset's recipe and
 * consumed by both binaries. It is host-endian; an endian sentinel rejects a
 * file written on a differently-endianed machine rather than loading garbage.
 * The recipe stays the source of truth; cooked files are regenerable and keyed
 * to their recipe via a hash carried in the header.
 *
 * Readers are defensive: every count/size is validated against the actual file
 * length before any allocation, so a corrupt or truncated file is rejected
 * instead of driving an oversized resize. Materials are not cooked here (they
 * are tiny scalar+ref JSON that loads directly); only meshes and textures are.
 */
namespace AssetCook {

/**
 * @brief Bump when the on-disk byte layout of the respective body changes.
 *
 * The version is written into each file's header and rejected on read if it
 * does not match, so a layout change forces a clean re-cook rather than
 * silently misreading an old file.
 */
constexpr uint16_t MESH_FORMAT_VERSION    = 1;
constexpr uint16_t TEXTURE_FORMAT_VERSION = 1;

// Writers (editor cooker). Create parent directories as needed. `recipeHash` is
// stored in the header for staleness checks. Return false on any IO error.
bool writeMesh   (const std::filesystem::path& path, const MeshAsset&    mesh,    uint64_t recipeHash);
bool writeTexture(const std::filesystem::path& path, const TextureAsset& texture, uint64_t recipeHash);

// Readers (both binaries). Fill `out` on success and, if `outHash` is non-null,
// report the stored recipe hash. Return false (logging the reason) on any
// magic / endian / version / size / integrity mismatch.
bool readMesh   (const std::filesystem::path& path, MeshAsset&    out, uint64_t* outHash = nullptr);
bool readTexture(const std::filesystem::path& path, TextureAsset& out, uint64_t* outHash = nullptr);

} // namespace AssetCook

} // namespace Vkm::Engine
