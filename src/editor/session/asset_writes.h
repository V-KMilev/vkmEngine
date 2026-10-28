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
 * What a save, Play and Save as Prefab write before their document, so a later load resolves
 * what it names. The terrains go first, since a terrain's cook reads its file; one that fails
 * does not stop the cook. Binaries bake in the background.
 *
 * @param resources The assets.
 * @return Whether the terrains were written, and whether the cook succeeded.
 */
AssetWrites writeAssets(ResourceManager& resources);

/**
 * @brief Keep a crash copy's assets: each changed terrain's body under @p terrainDir, and a library
 *        record of each asset with none, so the copy's names resolve.
 *
 * Changes nothing the project already holds: a terrain's file and a recorded asset - a material's
 * values included - are left as the author last saved them (AssetCooker::recordNewAssets).
 *
 * @param resources  The assets.
 * @param terrainDir Where the terrains' bodies go (keepTerrainCopies).
 * @return Whether the bodies were kept, and whether the new assets were recorded.
 */
AssetWrites keepAssets(ResourceManager& resources, const std::filesystem::path& terrainDir);

} // namespace Vkm::Engine
