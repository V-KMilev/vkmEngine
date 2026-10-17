#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "resource/asset/terrain_asset.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief A terrain as bytes: the .terrain file the editor writes into a project, and the body of its
 *        cooked file.
 *
 * Kept by name where the asset holds handles - its layers' materials, its plants' meshes and
 * materials - and resolved again against a graph that holds them (resolve()). The file is the
 * terrain's source: its cook copies the body, so what a project carries is what is drawn.
 */
namespace TerrainFile {

/// The magic a .terrain file starts with, then VERSION.
inline constexpr char MAGIC[4] = {'V', 'K', 'M', 'T'};

/// The body's layout; readBody() refuses a file of another.
inline constexpr uint32_t VERSION = 1;

/// The longest name a body keeps.
inline constexpr uint32_t MAX_NAME = 4096;

/// What a body names, in the order resolve() reads it back.
struct Names {
    std::array<std::string, MaterialAsset::MAX_LAYERS> layers;
    /// Per foliage layer, per part: its mesh's name, then its material's.
    std::vector<std::vector<std::pair<std::string, std::string>>> plants;
};

/**
 * @brief @p terrain's body: its grid, maps and plants, its handles as their assets' names.
 *
 * @param terrain   The terrain.
 * @param resources Names its handles; a hidden or unnamed asset is kept as no name, and warned of.
 * @return The bytes.
 */
std::vector<uint8_t> encode(const TerrainAsset& terrain, const ResourceManager& resources);

/**
 * @brief Read a body back into @p out, its handles left empty and their names in @p names.
 *
 * @param bytes What encode() wrote.
 * @param size  How many there are.
 * @param out   Filled when the body is sound.
 * @param names The names it keeps.
 * @param error Why it is not, when it is not.
 * @return False for a body cut short, too long, or out of a terrain's bounds.
 */
bool decode(const uint8_t* bytes, size_t size, TerrainAsset& out, Names& names, std::string& error);

/**
 * @brief Give @p terrain the handles @p names names, from @p resources; a name it does not hold
 *        stays empty, and is warned of.
 *
 * @param terrain   Filled by decode().
 * @param names     What decode() read.
 * @param resources Holds the materials and meshes named.
 */
void resolve(TerrainAsset& terrain, const Names& names, const ResourceManager& resources);

/**
 * @brief Write @p terrain to @p path as a .terrain file, through a temporary file.
 *
 * @param path      Where.
 * @param terrain   The terrain.
 * @param resources Names its handles.
 * @return False, having logged, when it could not be written.
 */
bool write(const std::filesystem::path& path, const TerrainAsset& terrain, const ResourceManager& resources);

/**
 * @brief The body of the .terrain file at @p path, its magic and version checked.
 *
 * @param path  The file.
 * @param body  Its body.
 * @param error Why it could not be read.
 * @return False when it could not.
 */
bool readBody(const std::filesystem::path& path, std::vector<uint8_t>& body, std::string& error);

} // namespace TerrainFile

} // namespace Vkm::Engine
