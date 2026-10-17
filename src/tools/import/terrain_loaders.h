#pragma once

#include <string>

#include "resource/asset/terrain_asset.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Import a .terrain file (TerrainFile) into a TerrainAsset, its layers' materials and its
 *        plants' meshes and materials found by name in @p resources.
 *
 * Synchronous. The project-relative path is the terrain's name, as a sound's is.
 *
 * @param filePath  Path to the file, absolute or project-relative.
 * @param resources The graph the terrain is added to, holding what it names.
 * @return The terrain, or an invalid handle when the file does not read.
 */
TerrainHandle loadTerrainFile(const std::string& filePath, ResourceManager& resources);

} // namespace Vkm::Engine
