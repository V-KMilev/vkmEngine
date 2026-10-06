#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "resource/asset_type.h"

namespace Vkm::Engine::AssetCooker {

/**
 * @brief Every file importing the source at @p projectRelativePath reads, as project references.
 *
 * The source, a .gltf's external buffers, and an .obj's material libraries plus the `<name>.mtl` beside
 * it: a re-export can change those while the source stays byte-identical. A cook folds them into the key
 * and records them (AssetRecord::sources). Images are not included; a model's maps are texture assets
 * keyed by their own files.
 *
 * @param projectRelativePath The `path` a recipe names; empty names no files.
 * @return The files, the source first; a .gltf that does not parse adds none.
 */
std::vector<std::string> sourceFiles(const std::string& projectRelativePath);

/**
 * @brief sourceFiles for a path on this machine rather than a project reference.
 *
 * For an importer on a worker: resolving a reference reads the project root, which is the main thread's.
 *
 * @param path The source file on this machine; empty names no files.
 * @return The files, the source first, as paths on this machine.
 */
std::vector<std::filesystem::path> sourceFilesAt(const std::filesystem::path& path);

/**
 * @brief Fold the bytes of the source file at @p projectRelativePath into @p seed.
 *
 * A recipe names its source file, not its content, so without this a re-export does not move the key.
 * By content, since write times differ between machines; at cook time, since the recipe is
 * version-controlled and describes the import, not one machine's bytes.
 *
 * @param projectRelativePath The `path` a recipe names; empty (a generator or procedural source) folds
 *                            nothing.
 * @param seed                Hash to continue from.
 * @return The seed folded with the file's content, or with a marker when the file cannot be read, so
 *         missing art differs from present art.
 */
uint64_t foldSourceContent(const std::string& projectRelativePath, uint64_t seed);

/**
 * @brief Fold the recorded key of the asset (@p type, @p name) into @p seed.
 *
 * For a derived asset - a level decimated from another mesh, a clip bound to a rig from another file -
 * whose recipe names its dependency only by name, so the key would not move when it changes. Read from
 * the manifest, so the dependency has to be cooked first.
 *
 * @param type The dependency's kind.
 * @param name The dependency's name; empty folds nothing.
 * @param seed Hash to continue from.
 * @return The seed folded with the dependency's key, or with a marker when the manifest has no record
 *         of it, so a missing dependency differs from a present one.
 */
uint64_t foldDependency(AssetType type, const std::string& name, uint64_t seed);

} // namespace Vkm::Engine::AssetCooker
