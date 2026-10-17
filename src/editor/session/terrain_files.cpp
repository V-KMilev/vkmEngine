#include "session/terrain_files.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "io/asset/terrain_file.h"
#include "io/project_paths.h"
#include "resource/asset_source_kind.h"
#include "resource/asset/terrain_asset.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

bool writeTerrainFiles(ResourceManager& resources) {
    bool written = true;
    resources.forEachOfType<TerrainAsset>([&](TerrainHandle handle, const TerrainAsset& terrain) {
        if (terrain.isHidden() || terrain.name().empty()) return;
        // Its name is its file's project path, wherever it was loaded from.
        const std::string& name = terrain.name();
        const std::vector<uint8_t> body = TerrainFile::encode(terrain, resources);
        std::vector<uint8_t> onDisk;
        std::string          error;
        const std::filesystem::path path = ProjectPaths::resolveProjectPath(name);
        if (!TerrainFile::readBody(path, onDisk, error) || onDisk != body) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (!TerrainFile::write(path, terrain, resources)) {
                written = false;
                return;
            }
        }
        nlohmann::json source = {{AssetSourceKey::KIND, AssetSourceKind::FILE}, {AssetSourceKey::PATH, name}};
        if (!terrain.hasSource() || terrain.sourceJson() != source) {
            resources.edit(handle).sourceJson() = std::move(source);
        }
    });
    return written;
}

void readTerrainFiles(ResourceManager& resources) {
    std::vector<TerrainHandle> terrains;
    resources.forEachOfType<TerrainAsset>([&](TerrainHandle handle, const TerrainAsset& terrain) {
        if (!terrain.isHidden() && !terrain.name().empty()) terrains.push_back(handle);
    });
    for (const TerrainHandle handle : terrains) {
        const TerrainAsset& terrain = resources.get(handle);
        std::vector<uint8_t> onDisk;
        std::string          error;
        // A terrain with no file yet, made during the session, has nothing to come back to.
        if (!TerrainFile::readBody(ProjectPaths::resolveProjectPath(terrain.name()), onDisk, error)) continue;
        if (onDisk == TerrainFile::encode(terrain, resources)) continue;
        TerrainAsset      read;
        TerrainFile::Names names;
        if (!TerrainFile::decode(onDisk.data(), onDisk.size(), read, names, error)) continue;
        TerrainFile::resolve(read, names, resources);
        read.sourceJson() = terrain.sourceJson();
        resources.swapValue(handle, read);
    }
}

} // namespace Vkm::Engine
