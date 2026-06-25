#define VKM_LOG_CATEGORY "ASSET_COOK"

#include "io/cooked_loader.h"

#include <filesystem>
#include <utility>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "io/asset_cook.h"
#include "io/asset_library.h"
#include "platform/threading/thread_pool.h"
#include "resource/resource_manager.h"
#include "system/async/async_load_queue.h"

namespace Engine {

MeshHandle requestCookedMeshAsync(const std::string& name, ResourceManager& resources) {
    // Name is the stable identity: an already-requested mesh returns the same
    // handle even if its read is still in flight (mirrors requestModelMeshAsync).
    if (auto existing = resources.findByName<MeshAsset>(name)) return existing;

    const AssetLibrary::Record* record = AssetLibrary::get().find(AssetType::Mesh, name);
    if (!record || record->cookedFile.empty()) {
        LOG_ERROR("Cooked mesh '%s' not found in asset library manifest", name.c_str());
        return {};
    }

    MeshAsset stub;
    stub.name    = name;
    stub.loading = true;
    stub.sourceJson() = {{"kind", "cooked"}, {"name", name}};
    const MeshHandle handle = resources.add(std::move(stub));

    const std::filesystem::path path = AssetLibrary::get().cookedPath(*record);
    const uint64_t expectHash = record->recipeHash;

    ThreadPool::get().addTask([handle, path, expectHash]() {
        MeshLoadCompletion completion;
        completion.handle = handle;

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

    return handle;
}

TextureHandle requestCookedTextureAsync(const std::string& name, ResourceManager& resources) {
    if (auto existing = resources.findByName<TextureAsset>(name)) return existing;

    const AssetLibrary::Record* record = AssetLibrary::get().find(AssetType::Texture, name);
    if (!record || record->cookedFile.empty()) {
        LOG_ERROR("Cooked texture '%s' not found in asset library manifest", name.c_str());
        return {};
    }

    TextureAsset stub;
    stub.name    = name;
    stub.loading = true;
    stub.sourceJson() = {{"kind", "cooked"}, {"name", name}};
    const TextureHandle handle = resources.add(std::move(stub));

    const std::filesystem::path path = AssetLibrary::get().cookedPath(*record);
    const uint64_t expectHash = record->recipeHash;

    ThreadPool::get().addTask([handle, path, expectHash]() {
        TextureLoadCompletion completion;
        completion.handle = handle;

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

    return handle;
}

} // namespace Engine
