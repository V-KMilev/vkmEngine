#pragma once

#include <cstdint>
#include <filesystem>

#include "resource/asset_type.h"

namespace Vkm::Engine {

struct AnimationClipAsset;
struct AudioClipAsset;
struct MeshAsset;
struct SkeletonAsset;
struct TextureAsset;

/**
 * @brief Cooked binary asset format (derived cache).
 *
 * Host-endian; an endian sentinel rejects a file written on a differently-endianed machine. Cooked
 * files are regenerable and keyed to their recipe by name (see cacheKey). Readers validate every
 * count against the file length before allocating. Materials load straight from JSON and are not
 * cooked; meshes, textures, skeletons, animation clips and sounds are.
 */
namespace AssetCook {

/**
 * @brief Bump when anything the cooker writes changes: a kind's byte layout, an importer flag, a mip
 *        policy, a vertex welding rule.
 *
 * Part of every cache key, so a bump re-bakes everything. Also written into each header, where a reader
 * refuses a mismatch - the case of a file copied under a name not baked for it. Only
 * testTheCookersOutputIsPinnedToItsVersion notices a forgotten bump that changes no layout.
 */
constexpr uint16_t COOKER_VERSION = 12;

/**
 * @brief The complete cache key for one cooked artifact.
 *
 * Covers what it was baked from, its kind and the cooker. It goes in the artifact's name, so a change
 * to any of them makes the old file one nobody looks for rather than a stale one.
 *
 * @param recipeHash Recorded hash of the recipe, its source art and dependencies
 *                   (AssetRecord::recipeHash).
 * @param type       Asset kind, mixed in with COOKER_VERSION.
 * @return The key the artifact is filed under.
 */
uint64_t cacheKey(uint64_t recipeHash, AssetType type);

/**
 * @brief Bone count past which a rig is refused as corrupt rather than read.
 *
 * A rejection threshold, not a capability: a full character rig lands near three hundred bones.
 */
constexpr uint32_t MAX_SKELETON_BONES = 1024;

/**
 * @brief Channel count past which a sound is refused as corrupt rather than read.
 *
 * A rejection threshold, not a capability: 7.1 is the outer edge of authored source material.
 */
constexpr uint32_t MAX_AUDIO_CHANNELS = 8;

/**
 * @brief Sample rate past which a clip is refused as corrupt rather than read.
 *
 * Twice the highest consumer hardware rate. The rate gives the clip's duration, so a wild one makes the
 * length wrong everywhere it is read.
 */
constexpr uint32_t MAX_AUDIO_SAMPLE_RATE = 384000;

/**
 * @brief Write one cooked file of each kind.
 *
 * Creates the parent directories and writes beside the target, renaming onto it once the write is whole.
 *
 * @param path Where the file goes, already named for its key.
 * @return False on any IO error.
 */
[[nodiscard]] bool writeMesh         (const std::filesystem::path& path, const MeshAsset&          mesh);
[[nodiscard]] bool writeTexture      (const std::filesystem::path& path, const TextureAsset&       texture);
[[nodiscard]] bool writeSkeleton     (const std::filesystem::path& path, const SkeletonAsset&      skeleton);
[[nodiscard]] bool writeAnimationClip(const std::filesystem::path& path, const AnimationClipAsset& clip);
[[nodiscard]] bool writeAudioClip    (const std::filesystem::path& path, const AudioClipAsset&     audio);

/**
 * @brief Whether the cooked file at @p path can still serve @p type.
 *
 * Reads the header and measures the file, nothing more. Not current: absent, not a cooked asset, another
 * kind, or a length other than the header's. Recipe and cooker version are in the name, so not checked.
 * A material is never current: it has no binary. Logs nothing; a stale cache is the caller's to report.
 *
 * @param type Asset type the file is expected to hold.
 * @param path Cooked file to probe, already named for its key.
 * @return True when this build can read that file.
 */
[[nodiscard]] bool isCookedCurrent(AssetType type, const std::filesystem::path& path);

/**
 * @brief Read one cooked file of each kind.
 *
 * @param path The cooked file.
 * @param out  Filled on success.
 * @return False, logging the reason, on any magic, endian, kind, version, size or integrity mismatch.
 */
[[nodiscard]] bool readMesh         (const std::filesystem::path& path, MeshAsset&          out);
[[nodiscard]] bool readTexture      (const std::filesystem::path& path, TextureAsset&       out);
[[nodiscard]] bool readSkeleton     (const std::filesystem::path& path, SkeletonAsset&      out);
[[nodiscard]] bool readAnimationClip(const std::filesystem::path& path, AnimationClipAsset& out);
[[nodiscard]] bool readAudioClip    (const std::filesystem::path& path, AudioClipAsset&     out);

} // namespace AssetCook

} // namespace Vkm::Engine
