#define VKM_LOG_CATEGORY "ASYNC_LOADER"

#include "system/async/async_loader_system.h"

#include <vector>

#include "logger.h"

#include "debug/profiler.h"
#include "resource/asset/mesh_asset.h"
#include "resource/resource_manager.h"
#include "resource/asset/texture_asset.h"
#include "resource/texture_format.h"
#include "system/async/async_load_queue.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Finalise one batch of drained completions against the ResourceManager.
 *
 * Shared skeleton for both asset kinds. The liveness guard is not optional:
 * rm.get on an asset destroyed between the worker pushing and this drain
 * (editor deletion) is UB. The loading flag is always cleared; the asset is
 * committed (bumping its version so the backend re-uploads) only when @p apply
 * reports success.
 *
 * The handle alone is not enough to identify the target. A scene load swaps the
 * whole asset graph, and the incoming one restarts at the same indices and
 * generations, so a completion minted against the outgoing graph still looks
 * alive - it just names a stranger. The uid recorded at request time is what
 * tells the two apart.
 */
template <typename Completion, typename Apply>
void finalize(ResourceManager& rm, std::vector<Completion> completions, Apply apply) {
    for (auto& c : completions) {
        if (!c.handle) continue;
        if (!rm.isAlive(c.handle)) {
            LOG_VERBOSE("Async handle %u dead before completion landed - dropping", c.handle.id());
            continue;
        }

        auto& asset = rm.edit(c.handle);
        if (asset.uid != c.assetUid) {
            LOG_VERBOSE("Async completion for a replaced asset (slot %u, now '%s') - dropping",
                c.handle.id(), asset.name.c_str());
            continue;
        }

        const bool applied = apply(asset, c);
        asset.loading = false;
        if (applied) rm.commit(c.handle);
    }
}

} // namespace

void finalizeAsyncLoads(ResourceManager& rm) {
    AsyncLoadQueue& queue = AsyncLoadQueue::get();

    finalize(rm, queue.drainTextures(), [](TextureAsset& asset, TextureLoadCompletion& c) {
        if (!c.success || c.pixelData.empty()) {
            LOG_WARNING("Async texture decode failed for '%s' - leaving asset empty",
                asset.filePath.c_str());
            return false;
        }

        if (c.hasParams) {
            // Cooked texture: params (incl. format/sRGB/wrap/filter) are exact.
            asset.params = c.params;
            asset.srgb   = (c.params.internalFormat == TextureInternalFormat::SRGB8 ||
                            c.params.internalFormat == TextureInternalFormat::SRGBA8);
        } else {
            asset.params.width          = c.width;
            asset.params.height         = c.height;
            asset.params.internalFormat = inferInternalFormat(c.channels, asset.srgb);
            asset.params.format         = inferFormat(c.channels);
            asset.params.type           = TexturePixelType::UnsignedByte;
        }
        asset.pixelData = std::move(c.pixelData);
        return true;
    });

    // The worker computed bounds alongside vertex extraction, so this is a pure move.
    finalize(rm, queue.drainMeshes(), [](MeshAsset& asset, MeshLoadCompletion& c) {
        if (!c.success || c.vertices.empty()) {
            LOG_WARNING("Async mesh decode failed for '%s' - leaving asset empty",
                asset.name.c_str());
            return false;
        }

        asset.vertices   = std::move(c.vertices);
        asset.indices    = std::move(c.indices);
        asset.skin       = std::move(c.skin);
        asset.skeleton   = std::move(c.skeleton);
        asset.boundsMin  = c.boundsMin;
        asset.boundsMax  = c.boundsMax;
        asset.skinRadius = c.skinRadius;
        return true;
    });
}

void AsyncLoaderSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("AsyncLoaderSystem");
    finalizeAsyncLoads(ctx.resources);
}

} // namespace Vkm::Engine
