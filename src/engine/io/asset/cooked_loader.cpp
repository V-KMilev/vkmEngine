#define VKM_LOG_CATEGORY "IO"

#include "io/asset/cooked_loader.h"

#include <filesystem>
#include <utility>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "io/asset/asset_cook.h"
#include "io/asset/asset_library.h"
#include "platform/threading/thread_pool.h"
#include "resource/resource_manager.h"
#include "system/async/async_load_queue.h"

namespace Vkm::Engine {

namespace {

template<typename Asset>
struct CookedRequest {
    Handle<Asset>         handle{};          ///< Returned to the caller as-is.
    uint64_t              uid = 0;           ///< Identity of the stub the completion belongs to.
    bool                  dispatch = false;  ///< Kick off the off-thread read?
    std::filesystem::path path{};            ///< Cooked file (valid when dispatch).
    uint64_t              expectHash = 0;    ///< Recipe hash to match (valid when dispatch).
};

// Shared preamble: the existing handle if the asset is already resident
// (dispatch=false), an invalid handle if the name has no cooked entry, or a
// fresh loading stub plus the file + recipe hash to read off-thread.
template<typename Asset>
CookedRequest<Asset> beginCookedRequest(const std::string& name, AssetType type,
                                        const char* what, ResourceManager& resources) {
    if (auto existing = resources.findByName<Asset>(name)) return {existing};

    // A material has no cooked blob - its recipe is its runtime form - so a name
    // that resolves to one is as unusable here as one the manifest never listed.
    const AssetRecord* record = AssetLibrary::get().find(type, name);
    if (!record || type == AssetType::Material) {
        LOG_ERROR("Cooked %s '%s' not found in asset library manifest", what, name.c_str());
        return {};
    }

    Asset stub;
    stub.name    = name;
    stub.loading = true;
    stub.sourceJson() = {{"kind", "cooked"}, {"name", name}};

    CookedRequest<Asset> req;
    req.handle     = resources.add(std::move(stub));
    req.uid        = resources.get(req.handle).uid;
    req.dispatch   = true;
    req.path       = AssetLibrary::cookedPath(type, name);
    req.expectHash = record->recipeHash;
    return req;
}

// Shared body of the synchronous loads: resolve the name, read the file, and
// register the asset under the name it was read by.
template<typename Asset>
Handle<Asset> loadCookedSynchronous(const std::string& name, AssetType type, const char* what,
                                    bool (*read)(const std::filesystem::path&, Asset&, uint64_t*),
                                    ResourceManager& resources) {
    if (auto existing = resources.findByName<Asset>(name)) return existing;

    const AssetRecord* record = AssetLibrary::get().find(type, name);
    if (!record) {
        LOG_ERROR("Cooked %s '%s' not found in asset library manifest", what, name.c_str());
        return {};
    }

    const std::filesystem::path path = AssetLibrary::cookedPath(type, name);
    Asset decoded;
    uint64_t gotHash = 0;
    if (!read(path, decoded, &gotHash)) return {};
    if (gotHash != record->recipeHash) {
        LOG_ERROR("Cooked %s '%s': recipe hash mismatch - cache is stale", what, path.string().c_str());
        return {};
    }

    decoded.name = name;
    decoded.sourceJson() = {{"kind", "cooked"}, {"name", name}};
    return resources.add(std::move(decoded));
}

} // namespace

MeshHandle requestCookedMeshAsync(const std::string& name, ResourceManager& resources) {
    // Name is the stable identity: an already-requested mesh returns the same
    // handle even if its read is still in flight.
    auto req = beginCookedRequest<MeshAsset>(name, AssetType::Mesh, "mesh", resources);
    if (!req.dispatch) return req.handle;

    ThreadPool::get().addTask([handle = req.handle, uid = req.uid, path = req.path,
                               expectHash = req.expectHash]() {
        MeshLoadCompletion completion;
        completion.handle   = handle;
        completion.assetUid = uid;

        MeshAsset decoded;
        uint64_t gotHash = 0;
        const bool ok = AssetCook::readMesh(path, decoded, &gotHash);
        if (ok && gotHash == expectHash) {
            completion.vertices  = std::move(decoded.vertices);
            completion.indices   = std::move(decoded.indices);
            completion.boundsMin = decoded.boundsMin;
            completion.boundsMax = decoded.boundsMax;
            completion.success   = !completion.vertices.empty();
        } else if (ok) {
            LOG_ERROR("Cooked mesh '%s': recipe hash mismatch - cache is stale", path.string().c_str());
        }
        AsyncLoadQueue::get().pushMesh(std::move(completion));
    });

    return req.handle;
}

TextureHandle requestCookedTextureAsync(const std::string& name, ResourceManager& resources) {
    auto req = beginCookedRequest<TextureAsset>(name, AssetType::Texture, "texture", resources);
    if (!req.dispatch) return req.handle;

    ThreadPool::get().addTask([handle = req.handle, uid = req.uid, path = req.path,
                               expectHash = req.expectHash]() {
        TextureLoadCompletion completion;
        completion.handle   = handle;
        completion.assetUid = uid;

        TextureAsset decoded;
        uint64_t gotHash = 0;
        const bool ok = AssetCook::readTexture(path, decoded, &gotHash);
        if (ok && gotHash == expectHash) {
            completion.params    = decoded.params;
            completion.hasParams = true;
            completion.pixelData = std::move(decoded.pixelData);
            completion.success   = !completion.pixelData.empty();
        } else if (ok) {
            LOG_ERROR("Cooked texture '%s': recipe hash mismatch - cache is stale", path.string().c_str());
        }
        AsyncLoadQueue::get().pushTexture(std::move(completion));
    });

    return req.handle;
}

SkeletonHandle loadCookedSkeleton(const std::string& name, ResourceManager& resources) {
    return loadCookedSynchronous<SkeletonAsset>(name, AssetType::Skeleton, "skeleton",
                                                &AssetCook::readSkeleton, resources);
}

AnimationClipHandle loadCookedAnimationClip(const std::string& name, ResourceManager& resources) {
    return loadCookedSynchronous<AnimationClipAsset>(name, AssetType::AnimationClip, "clip",
                                                     &AssetCook::readAnimationClip, resources);
}

} // namespace Vkm::Engine
