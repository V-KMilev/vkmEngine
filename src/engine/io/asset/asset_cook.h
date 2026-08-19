#pragma once

#include <cstdint>
#include <filesystem>

namespace Vkm::Engine {

struct AnimationClipAsset;
struct MeshAsset;
struct SkeletonAsset;
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
 * are tiny scalar+ref JSON that loads directly); meshes, textures, skeletons
 * and animation clips are.
 */
namespace AssetCook {

/**
 * @brief Bump when the on-disk byte layout of the respective body changes.
 *
 * The version is written into each file's header and rejected on read if it
 * does not match, so a layout change forces a clean re-cook rather than
 * silently misreading an old file.
 */
constexpr uint16_t MESH_FORMAT_VERSION           = 1;
constexpr uint16_t TEXTURE_FORMAT_VERSION        = 1;
constexpr uint16_t SKELETON_FORMAT_VERSION       = 1;
constexpr uint16_t ANIMATION_CLIP_FORMAT_VERSION = 1;

/**
 * @brief Bone count past which a rig is refused as corrupt rather than read.
 *
 * A rejection threshold, not a capability: a full character rig with face and
 * fingers lands near three hundred bones, so nothing real approaches this,
 * while a count read out of a damaged file usually does not land under it by
 * accident. Raising it later accepts strictly more files and breaks nothing;
 * lowering it would refuse files already on disk, so it starts tight.
 */
constexpr uint32_t MAX_SKELETON_BONES = 1024;

// Writers (editor cooker). Create parent directories as needed. `recipeHash` is
// stored in the header for staleness checks. Return false on any IO error.
bool writeMesh         (const std::filesystem::path& path, const MeshAsset&          mesh,     uint64_t recipeHash);
bool writeTexture      (const std::filesystem::path& path, const TextureAsset&       texture,  uint64_t recipeHash);
bool writeSkeleton     (const std::filesystem::path& path, const SkeletonAsset&      skeleton, uint64_t recipeHash);
bool writeAnimationClip(const std::filesystem::path& path, const AnimationClipAsset& clip,     uint64_t recipeHash);

// Readers (both binaries). Fill `out` on success and, if `outHash` is non-null,
// report the stored recipe hash. Return false (logging the reason) on any
// magic / endian / version / size / integrity mismatch.
bool readMesh         (const std::filesystem::path& path, MeshAsset&          out, uint64_t* outHash = nullptr);
bool readTexture      (const std::filesystem::path& path, TextureAsset&       out, uint64_t* outHash = nullptr);
bool readSkeleton     (const std::filesystem::path& path, SkeletonAsset&      out, uint64_t* outHash = nullptr);
bool readAnimationClip(const std::filesystem::path& path, AnimationClipAsset& out, uint64_t* outHash = nullptr);

} // namespace AssetCook

} // namespace Vkm::Engine
