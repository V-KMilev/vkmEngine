#include "session/asset_writes.h"

#include "cook/asset_cooker.h"
#include "session/terrain_files.h"

namespace Vkm::Engine {

AssetWrites writeAssets(ResourceManager& resources) {
    AssetWrites writes;
    writes.terrains = writeTerrainFiles(resources);
    writes.cooked   = AssetCooker::cookAllAssets(resources, AssetCooker::Bake::InBackground);
    return writes;
}

AssetWrites keepAssets(ResourceManager& resources, const std::filesystem::path& terrainDir) {
    AssetWrites writes;
    writes.terrains = keepTerrainCopies(resources, terrainDir);
    writes.cooked   = AssetCooker::recordNewAssets(resources);
    return writes;
}

} // namespace Vkm::Engine
