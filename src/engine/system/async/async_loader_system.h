#pragma once

#include "core/system.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Finalise every completion waiting in the AsyncLoadQueue against @p resources.
 *
 * What AsyncLoaderSystem does, without a frame. A host that has no update loop
 * still puts assets in flight the moment it loads a scene, and still has to land
 * them before anything reads their contents - the cooker being the one that
 * does, since an asset still decoding has no vertices to bake.
 *
 * Idempotent and cheap on an empty queue, so a caller with no frames of its own
 * can poll it.
 *
 * @param resources Resource manager holding the assets the completions name.
 */
void finalizeAsyncLoads(ResourceManager& resources);

/**
 * @brief Finalise completions until nothing in @p resources is still loading.
 *
 * For a caller that cannot proceed on a stub: the cooker, which has no vertices
 * to bake until the import lands, and the `decimate` mesh recipe, which would
 * quietly produce no level at all from a base that has not arrived. Both run
 * where there is no next frame to wait for.
 *
 * Gives up after a bounded wait rather than blocking forever, because a worker
 * that died without pushing its completion would otherwise hang a build with
 * nothing in the log.
 *
 * @param resources Resource manager holding the assets being waited on.
 * @return False when assets were still loading at the deadline (logged).
 */
bool awaitAsyncLoads(ResourceManager& resources);

/**
 * @brief Drains the AsyncLoadQueue once per frame and finalises completed
 *        asset loads against the live ResourceManager.
 *
 * Workers on the ThreadPool decode pixels / parse meshes off the main thread
 * and push completions into the queue. This system runs at the Simulation
 * stage so completions land before VisibilitySystem + RenderSystem, giving
 * the backend a chance to upload the finalised resource in the same frame.
 *
 * Resource lookups are guarded with isAlive() because a texture can be
 * destroyed by the editor (or by a scene swap) between the worker
 * pushing and this system draining.
 */
class AsyncLoaderSystem : public System {
    public:
        AsyncLoaderSystem() = default;
        ~AsyncLoaderSystem() override = default;

        AsyncLoaderSystem(const AsyncLoaderSystem& other) = delete;
        AsyncLoaderSystem& operator=(const AsyncLoaderSystem& other) = delete;

        AsyncLoaderSystem(AsyncLoaderSystem && other) = delete;
        AsyncLoaderSystem& operator=(AsyncLoaderSystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;
};

} // namespace Vkm::Engine
