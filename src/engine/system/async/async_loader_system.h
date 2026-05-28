#pragma once

#include "core/system.h"

namespace Engine {

/**
 * @brief Drains the AsyncLoadQueue once per frame and finalises completed
 *        asset loads against the live ResourceManager.
 *
 * Workers running on the ThreadPool decode pixels off the main thread,
 * then push a TextureLoadCompletion into the queue. This system runs at
 * the Simulation stage so completions land before VisibilitySystem +
 * RenderSystem, giving the backend a chance to upload the finalised
 * texture in the same frame.
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

} // namespace Engine
