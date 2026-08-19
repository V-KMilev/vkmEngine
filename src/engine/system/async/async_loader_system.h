#pragma once

#include "core/system.h"

namespace Vkm::Engine {

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
