#define VKM_LOG_CATEGORY "LOADER"

#include "import/terrain_loaders.h"

#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "io/asset/terrain_file.h"
#include "io/project_paths.h"
#include "resource/asset_source_kind.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

TerrainHandle loadTerrainFile(const std::string& filePath, ResourceManager& resources) {
    const std::string ref = ProjectPaths::toProjectRelative(filePath);
    if (const TerrainHandle existing = resources.findByName<TerrainAsset>(ref)) return existing;

    std::vector<uint8_t> body;
    std::string          error;
    TerrainAsset         terrain;
    TerrainFile::Names   names;
    if (!TerrainFile::readBody(ProjectPaths::resolveProjectPath(ref), body, error)
        || !TerrainFile::decode(body.data(), body.size(), terrain, names, error)) {
        LOG_ERROR("Terrain '%s' %s", ref.c_str(), error.c_str());
        return {};
    }
    TerrainFile::resolve(terrain, names, resources);
    terrain.sourceJson() = {{AssetSourceKey::KIND, AssetSourceKind::FILE}, {AssetSourceKey::PATH, ref}};
    return resources.add(std::move(terrain), ref);
}

} // namespace Vkm::Engine
