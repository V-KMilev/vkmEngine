#include "io/asset/cooked_loader.h"

#include <filesystem>
#include <utility>

#include <nlohmann/json.hpp>

#include "debug/engine_error_log.h"
#include "io/asset/asset_cook.h"
#include "io/asset/asset_factory.h"
#include "io/asset/asset_library.h"
#include "io/asset/terrain_file.h"
#include "platform/threading/thread_pool.h"
#include "resource/asset_source_kind.h"
#include "resource/resource_manager.h"
#include "system/async/async_load_queue.h"

namespace Vkm::Engine {

namespace {

// The cooked file the manifest's row for @p name names, or an empty path when there is no row or this
// build cannot read the file.
template<typename Asset>
std::filesystem::path currentCookedFile(const std::string& name) {
    constexpr AssetType TYPE = ASSET_TYPE<Asset>;
    const AssetRecord* record = AssetLibrary::get().find(TYPE, name);
    if (!record) return {};
    // A recipe edited since its cook names something else, which a host that can import it does
    // instead; one that cannot is better served by the file than by nothing.
    if (recipeImport<Asset>()) {
        nlohmann::json recipe;
        const bool read = AssetLibrary::readRecipe(TYPE, name, recipe);
        if (read && AssetRecord::documentHashOf(recipe) != record->documentHash) return {};
    }
    std::filesystem::path path = AssetLibrary::cookedPath(TYPE, name, record->recipeHash);
    return AssetCook::isCookedCurrent(TYPE, path) ? path : std::filesystem::path{};
}

// A cooked file whose header passed but whose body the reader refused: gone where the host can
// import its recipe, so the next cook bakes it again instead of taking it as current. A host
// without that import keeps what it was given.
template<typename Asset>
void dropUnreadable(const std::filesystem::path& path) {
    if (!recipeImport<Asset>()) return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// A file read now that the reader refused: left out (dropUnreadable), and said, as an async
// completion says it.
template<typename Asset>
void refuseNow(const std::filesystem::path& path, const std::string& name) {
    dropUnreadable<Asset>(path);
    reportError("Asset", name, "its file could not be read - it is left out");
}

// What a cache-served asset carries for a source: the cooker reads it as "nothing to re-cook".
nlohmann::json cookedSource() {
    return {{AssetSourceKey::KIND, AssetSourceKind::COOKED}};
}

// A loading stub under @p name, whose cooked file @p read decodes on the ThreadPool.
template<typename Asset, typename Read>
Handle<Asset> loadAsync(const std::string& name, ResourceManager& resources, Read read) {
    if (const Handle<Asset> resident = resources.findByName<Asset>(name)) return resident;
    std::filesystem::path path = currentCookedFile<Asset>(name);
    if (path.empty()) return {};

    Asset stub;
    stub.loading      = true;
    stub.sourceJson() = cookedSource();
    const Handle<Asset> handle = resources.add(std::move(stub), name);
    const uint64_t      uid    = resources.get(handle).uid();
    ThreadPool::get().addTask([handle, uid, path = std::move(path), read]() { read(handle, uid, path); });
    return handle;
}

// Read the cooked file for @p name now and register the asset under that name.
template<typename Asset>
Handle<Asset> loadNow(
    const std::string& name,
    ResourceManager& resources,
    bool (*read)(const std::filesystem::path&, Asset&)
) {
    if (const Handle<Asset> resident = resources.findByName<Asset>(name)) return resident;
    const std::filesystem::path path = currentCookedFile<Asset>(name);
    if (path.empty()) return {};

    Asset decoded;
    if (!read(path, decoded)) {
        refuseNow<Asset>(path, name);
        return {};
    }
    decoded.sourceJson() = cookedSource();
    return resources.add(std::move(decoded), name);
}

} // namespace

MeshHandle loadCookedMesh(const std::string& name, ResourceManager& resources) {
    return loadAsync<MeshAsset>(
        name,
        resources,
        [](MeshHandle handle, uint64_t uid, const std::filesystem::path& path) {
            MeshAsset decoded;
            if (!AssetCook::readMesh(path, decoded)) {
                decoded = MeshAsset{};
                dropUnreadable<MeshAsset>(path);
            }
            AsyncLoadQueue::get().pushMesh({handle, uid, std::move(decoded)});
        }
    );
}

TextureHandle loadCookedTexture(const std::string& name, ResourceManager& resources) {
    return loadAsync<TextureAsset>(
        name,
        resources,
        [](TextureHandle handle, uint64_t uid, const std::filesystem::path& path) {
            TextureAsset decoded;
            if (!AssetCook::readTexture(path, decoded)) {
                decoded = TextureAsset{};
                dropUnreadable<TextureAsset>(path);
            }
            AsyncLoadQueue::get().pushTexture({handle, uid, std::move(decoded)});
        }
    );
}

SkeletonHandle loadCookedSkeleton(const std::string& name, ResourceManager& resources) {
    return loadNow<SkeletonAsset>(name, resources, &AssetCook::readSkeleton);
}

AnimationClipHandle loadCookedAnimationClip(const std::string& name, ResourceManager& resources) {
    return loadNow<AnimationClipAsset>(name, resources, &AssetCook::readAnimationClip);
}

AudioClipHandle loadCookedAudioClip(const std::string& name, ResourceManager& resources) {
    return loadNow<AudioClipAsset>(name, resources, &AssetCook::readAudioClip);
}

TerrainHandle loadCookedTerrain(const std::string& name, ResourceManager& resources) {
    if (const TerrainHandle resident = resources.findByName<TerrainAsset>(name)) return resident;
    const std::filesystem::path path = currentCookedFile<TerrainAsset>(name);
    if (path.empty()) return {};

    TerrainAsset       decoded;
    TerrainFile::Names names;
    if (!AssetCook::readTerrain(path, decoded, names)) {
        refuseNow<TerrainAsset>(path, name);
        return {};
    }
    TerrainFile::resolve(decoded, names, resources);
    decoded.sourceJson() = cookedSource();
    return resources.add(std::move(decoded), name);
}

} // namespace Vkm::Engine
