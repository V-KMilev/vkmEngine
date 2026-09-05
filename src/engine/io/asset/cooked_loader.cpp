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
#include "resource/asset_source_kind.h"

namespace Vkm::Engine {

namespace {

template<typename Asset>
struct CookedRequest {
    Handle<Asset>         handle{};          ///< Returned to the caller as-is.
    uint64_t              uid = 0;           ///< Identity of the stub the completion belongs to.
    bool                  dispatch = false;  ///< Kick off the off-thread read?
    std::filesystem::path path{};            ///< Cooked file (valid when dispatch).
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
    stub.loading = true;
    stub.sourceJson() = {{"kind", AssetSourceKind::COOKED}, {"name", name}};

    CookedRequest<Asset> req;
    req.handle     = resources.add(std::move(stub), name);
    req.uid        = resources.get(req.handle).uid();
    req.dispatch   = true;
    req.path       = AssetLibrary::cookedPath(type, name,
                                              AssetCook::cacheKey(record->recipeHash, type));
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

    // No hash comparison: the path was composed from the key, so a file found
    // there was baked from this recipe by this cooker for this layout. A stale
    // artifact is not read and rejected, it is not looked for.
    const std::filesystem::path path =
        AssetLibrary::cookedPath(type, name, AssetCook::cacheKey(record->recipeHash, type));
    Asset decoded;
    if (!read(path, decoded, nullptr)) return {};

    decoded.sourceJson() = {{"kind", AssetSourceKind::COOKED}, {"name", name}};
    return resources.add(std::move(decoded), name);
}

} // namespace

MeshHandle requestCookedMeshAsync(const std::string& name, ResourceManager& resources) {
    // Name is the stable identity: an already-requested mesh returns the same
    // handle even if its read is still in flight.
    auto req = beginCookedRequest<MeshAsset>(name, AssetType::Mesh, "mesh", resources);
    if (!req.dispatch) return req.handle;

    ThreadPool::get().addTask([handle = req.handle, uid = req.uid, path = req.path]() {
        MeshLoadCompletion completion;
        completion.handle   = handle;
        completion.assetUid = uid;

        MeshAsset decoded;
        if (AssetCook::readMesh(path, decoded, nullptr)) {
            completion.vertices   = std::move(decoded.vertices);
            completion.indices    = std::move(decoded.indices);
            completion.skin       = std::move(decoded.skin);
            completion.skeleton   = std::move(decoded.skeleton);
            completion.boundsMin  = decoded.boundsMin;
            completion.boundsMax  = decoded.boundsMax;
            completion.skinRadius = decoded.skinRadius;
            completion.success    = !completion.vertices.empty();
        }
        AsyncLoadQueue::get().pushMesh(std::move(completion));
    });

    return req.handle;
}

TextureHandle requestCookedTextureAsync(const std::string& name, ResourceManager& resources) {
    auto req = beginCookedRequest<TextureAsset>(name, AssetType::Texture, "texture", resources);
    if (!req.dispatch) return req.handle;

    ThreadPool::get().addTask([handle = req.handle, uid = req.uid, path = req.path]() {
        TextureLoadCompletion completion;
        completion.handle   = handle;
        completion.assetUid = uid;

        TextureAsset decoded;
        if (AssetCook::readTexture(path, decoded, nullptr)) {
            completion.params    = decoded.params;
            completion.hasParams = true;
            completion.pixelData = std::move(decoded.pixelData);
            completion.success   = !completion.pixelData.empty();
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

AudioClipHandle loadCookedAudioClip(const std::string& name, ResourceManager& resources) {
    return loadCookedSynchronous<AudioClipAsset>(name, AssetType::AudioClip, "sound",
                                                 &AssetCook::readAudioClip, resources);
}

} // namespace Vkm::Engine
