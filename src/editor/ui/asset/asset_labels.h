#pragma once

#include <string>

namespace Vkm::Engine {

/**
 * @brief The stem of the file an asset's name names: "rocks" of "a/rocks.gltf:mesh0".
 *
 * The file is what comes before the first ':', when that holds a '/' or a '.'; a name that names
 * none, "mesh:generator:cube", gives empty.
 *
 * @param name The asset's name.
 * @return The stem, or empty for a name that names no file.
 */
std::string assetFileStem(const std::string& name);

/**
 * @brief An asset's name cut to a word: its file's stem, or the last part of a name with no file.
 *
 * The part inside a file is dropped: "a/rocks.gltf:mesh0" reads "rocks", "mesh:generator:cube" "cube".
 *
 * @param name The asset's name.
 * @return The short name.
 */
std::string assetShortName(const std::string& name);

} // namespace Vkm::Engine
