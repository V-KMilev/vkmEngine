#pragma once

#include <filesystem>

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief What a write of a scene's assets managed, each half on its own.
 */
struct AssetWrites {
    bool terrains = false;  ///< Every terrain that needed writing was written.
    bool cooked   = false;  ///< Every asset cooked and the manifest saved.
};

/**
 * @brief Write each changed terrain's file, then cook every loaded asset into the library.
 *
 * What a save, Play and Save as Prefab write before their document, so a later load resolves what it
 * names. A terrain that fails does not stop the cook; binaries bake in the background.
 *
 * @param resources The assets.
 * @return Whether the terrains were written, and whether the cook succeeded.
 */
AssetWrites writeAssets(ResourceManager& resources);

/**
 * @brief For a crash copy, keep each changed terrain's body under @p terrainDir and record new assets.
 *
 * So the copy's names resolve. Nothing the project holds changes (AssetCooker::recordNewAssets).
 *
 * @param resources  The assets.
 * @param terrainDir Where the terrains' bodies go (keepTerrainCopies).
 * @return Whether the bodies were kept, and whether the new assets were recorded.
 */
AssetWrites keepAssets(ResourceManager& resources, const std::filesystem::path& terrainDir);

} // namespace Vkm::Engine
