#include "session/terrain_files.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/fnv1a.h"
#include "io/asset/terrain_file.h"
#include "io/project_paths.h"
#include "resource/asset_source_kind.h"
#include "resource/asset/terrain_asset.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {

/// Where keepTerrainCopies keeps @p name's body under @p dir.
std::filesystem::path copyOf(const std::filesystem::path& dir, const std::string& name) {
    char file[32];
    const auto hash = static_cast<unsigned long long>(fnv1a64(name));
    std::snprintf(file, sizeof(file), "%016llx.terrain", hash);
    return dir / file;
}

/// The terrains the files are for: named and not hidden. Collected first, so each may be swapped.
std::vector<TerrainHandle> namedTerrains(const ResourceManager& resources) {
    std::vector<TerrainHandle> terrains;
    resources.forEachOfType<TerrainAsset>([&](TerrainHandle handle, const TerrainAsset& terrain) {
        if (!terrain.isHidden() && !terrain.name().empty()) terrains.push_back(handle);
    });
    return terrains;
}

/// Whether @p terrain's .terrain file holds it as it is; false for one with no readable file.
bool matchesItsFile(const TerrainAsset& terrain, const ResourceManager& resources) {
    std::vector<uint8_t> onDisk;
    std::string          error;
    const std::filesystem::path file = ProjectPaths::resolveProjectPath(terrain.name());
    return TerrainFile::readBody(file, onDisk, error) && onDisk == TerrainFile::encode(terrain, resources);
}

/// Put @p handle's terrain back as @p body holds it, in place, keeping the file it names; a body
/// that does not decode leaves it as it is.
void swapInBody(ResourceManager& resources, TerrainHandle handle, const std::vector<uint8_t>& body) {
    TerrainAsset       read;
    TerrainFile::Names names;
    std::string        error;
    if (!TerrainFile::decode(body.data(), body.size(), read, names, error)) return;
    TerrainFile::resolve(read, names, resources);
    // One made since the last save has no file to name yet.
    const TerrainAsset& terrain = resources.get(handle);
    if (terrain.hasSource()) read.sourceJson() = terrain.sourceJson();
    resources.swapValue(handle, read);
}

} // namespace

bool writeTerrainFiles(ResourceManager& resources) {
    bool written = true;
    for (const TerrainHandle handle : namedTerrains(resources)) {
        const TerrainAsset& terrain = resources.get(handle);
        // Its name is its file's project path, wherever it was loaded from.
        const std::string& name = terrain.name();
        if (!matchesItsFile(terrain, resources)) {
            const std::filesystem::path path = ProjectPaths::resolveProjectPath(name);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (!TerrainFile::write(path, terrain, resources)) {
                written = false;
                continue;
            }
        }
        nlohmann::json source = {{AssetSourceKey::KIND, AssetSourceKind::FILE}, {AssetSourceKey::PATH, name}};
        if (!terrain.hasSource() || terrain.sourceJson() != source) {
            resources.edit(handle).sourceJson() = std::move(source);
        }
    }
    return written;
}

void readTerrainFiles(ResourceManager& resources) {
    for (const TerrainHandle handle : namedTerrains(resources)) {
        // A terrain with no file yet, made during the session, has nothing to come back to.
        std::vector<uint8_t> onDisk;
        std::string          error;
        const std::filesystem::path file = ProjectPaths::resolveProjectPath(resources.get(handle).name());
        if (!TerrainFile::readBody(file, onDisk, error)) continue;
        if (onDisk == TerrainFile::encode(resources.get(handle), resources)) continue;
        swapInBody(resources, handle, onDisk);
    }
}

bool keepTerrainCopies(const ResourceManager& resources, const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    bool kept = true;
    for (const TerrainHandle handle : namedTerrains(resources)) {
        const TerrainAsset& terrain = resources.get(handle);
        if (matchesItsFile(terrain, resources)) continue;
        std::filesystem::create_directories(dir, ec);
        kept = TerrainFile::write(copyOf(dir, terrain.name()), terrain, resources) && kept;
    }
    return kept;
}

void readTerrainCopies(ResourceManager& resources, const std::filesystem::path& dir) {
    for (const TerrainHandle handle : namedTerrains(resources)) {
        std::vector<uint8_t> kept;
        std::string          error;
        if (!TerrainFile::readBody(copyOf(dir, resources.get(handle).name()), kept, error)) continue;
        swapInBody(resources, handle, kept);
    }
}

} // namespace Vkm::Engine
