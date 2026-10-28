#define VKM_LOG_CATEGORY "ASYNC_LOADER"

#include "system/async/async_loader_system.h"

#include <chrono>
#include <cstdint>
#include <thread>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "debug/engine_error_log.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "resource/asset/mesh_asset.h"
#include "resource/resource_manager.h"
#include "resource/asset/texture_asset.h"
#include "system/async/async_load_queue.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief The uid of every asset in @p resources still waiting on its decode.
 *
 * @param resources Graph to scan.
 * @return The uids of its MeshAssets and TextureAssets whose `loading` flag is set.
 */
std::unordered_set<uint64_t> inFlight(const ResourceManager& resources) {
    std::unordered_set<uint64_t> pending;
    resources.forEachOfType<MeshAsset>([&](MeshHandle, const MeshAsset& mesh) {
        if (mesh.loading) pending.insert(mesh.uid());
    });
    resources.forEachOfType<TextureAsset>([&](TextureHandle, const TextureAsset& tex) {
        if (tex.loading) pending.insert(tex.uid());
    });
    return pending;
}

/**
 * @brief The completions among @p drained that land on an asset in @p pending.
 *
 * Every other one goes back on the queue through @p putBack.
 *
 * @tparam Completion The queue's completion type.
 * @param drained Completions just drained; consumed.
 * @param pending Uids of this graph's assets still loading.
 * @param putBack The queue's push for this completion type.
 * @return This graph's completions.
 */
template <typename Completion>
std::vector<Completion> keepOwn(
    std::vector<Completion> drained,
    const std::unordered_set<uint64_t>& pending,
    void (AsyncLoadQueue::*putBack)(Completion)
) {
    std::vector<Completion> own;
    for (Completion& c : drained) {
        if (pending.count(c.assetUid) > 0) own.push_back(std::move(c));
        else (AsyncLoadQueue::get().*putBack)(std::move(c));
    }
    return own;
}

// Whether a decode produced anything to draw.
bool hasContent(const MeshAsset& mesh)       { return !mesh.vertices.empty(); }
bool hasContent(const TextureAsset& texture) { return !texture.pixelData.empty(); }

/**
 * @brief Land one batch of drained completions on their assets.
 *
 * Liveness is checked first: rm.edit on an asset destroyed since the push reads a freed slot. The
 * uid, not the handle, identifies the target, since a replacement graph reuses indices and
 * generations (see ResourceManager::epoch). The decode is swapped into the slot, which keeps its
 * identity and source and moves its version; a failed one only clears the loading flag.
 *
 * @tparam Asset MeshAsset or TextureAsset.
 * @param rm Resource manager holding the target assets.
 * @param completions The batch to land; consumed.
 * @return How many of its own assets came out empty.
 */
template <typename Asset>
size_t finalize(ResourceManager& rm, std::vector<LoadCompletion<Asset>> completions) {
    size_t failed = 0;
    for (LoadCompletion<Asset>& c : completions) {
        if (!c.handle) continue;
        if (!rm.isAlive(c.handle)) {
            LOG_VERBOSE("Async handle %u dead before completion landed - dropping", c.handle.id());
            continue;
        }

        Asset& live = rm.edit(c.handle);
        if (live.uid() != c.assetUid) {
            LOG_VERBOSE(
                "Async completion for a replaced asset (slot %u, now '%s') - dropping",
                c.handle.id(),
                live.name().c_str()
            );
            continue;
        }

        // An error, not a warning: a host's error sink is how a cook learns an asset came out empty.
        if (!hasContent(c.decoded)) {
            reportError("Asset", live.name(), "its file could not be read - it is left empty");
            live.loading = false;
            ++failed;
            continue;
        }
        if (live.hasSource()) c.decoded.sourceJson() = live.sourceJson();
        c.decoded.loading = false;
        rm.swapValue(c.handle, c.decoded);
    }
    return failed;
}

} // namespace

size_t assetsLoading(const ResourceManager& resources) {
    size_t count = meshesLoading(resources);
    resources.forEachOfType<TextureAsset>([&](TextureHandle, const TextureAsset& texture) {
        count += texture.loading;
    });
    return count;
}

size_t meshesLoading(const ResourceManager& resources) {
    size_t count = 0;
    resources.forEachOfType<MeshAsset>([&](MeshHandle, const MeshAsset& mesh) { count += mesh.loading; });
    return count;
}

void finalizeAsyncLoads(ResourceManager& rm) {
    AsyncLoadQueue& queue = AsyncLoadQueue::get();
    finalize(rm, queue.drainTextures());
    finalize(rm, queue.drainMeshes());
}

bool awaitAsyncLoads(ResourceManager& resources, std::chrono::milliseconds patience) {
    AsyncLoadQueue& queue = AsyncLoadQueue::get();
    auto   deadline  = std::chrono::steady_clock::now() + patience;
    size_t waitingOn = SIZE_MAX;
    size_t failed    = 0;
    for (;;) {
        const std::unordered_set<uint64_t> pending = inFlight(resources);
        if (pending.empty()) return failed == 0;
        const auto now = std::chrono::steady_clock::now();
        if (pending.size() < waitingOn) {
            waitingOn = pending.size();
            deadline  = now + patience;
        }
        if (now >= deadline) {
            LOG_ERROR(
                "%zu asset(s) still loading after %lld ms with none landing; giving up on them",
                pending.size(),
                static_cast<long long>(patience.count())
            );
            return false;
        }
        // This graph's completions only - the declaration says why.
        failed += finalize(resources, keepOwn(queue.drainTextures(), pending, &AsyncLoadQueue::pushTexture));
        failed += finalize(resources, keepOwn(queue.drainMeshes(), pending, &AsyncLoadQueue::pushMesh));
        if (inFlight(resources).empty()) return failed == 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool ArrivalHold::holds(const Scene& scene, const ResourceManager& resources) {
    const auto now = std::chrono::steady_clock::now();
    if (!m_seen || scene.epoch() != m_epoch) {
        m_seen    = true;
        m_epoch   = scene.epoch();
        m_waiting = true;
        m_left    = SIZE_MAX;
    }
    if (!m_waiting) return false;

    // A landing restarts the patience; a load starting to decode does not.
    const size_t left = m_loading(resources);
    if (left < m_left) m_since = now;
    m_left = left;
    if (left == 0) {
        m_waiting = false;
    } else if (now - m_since >= m_patience) {
        LOG_ERROR(
            "The world waited %lld ms on %zu asset(s) with none landing; %s without them",
            static_cast<long long>(m_patience.count()),
            left,
            m_goesOn
        );
        m_waiting = false;
    }
    return m_waiting;
}

void AsyncLoaderSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("AsyncLoaderSystem");
    finalizeAsyncLoads(ctx.resources);
}

} // namespace Vkm::Engine
