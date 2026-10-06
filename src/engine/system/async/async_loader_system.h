#pragma once

#include <chrono>

#include "core/system.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Finalise every completion waiting in the AsyncLoadQueue against @p resources.
 *
 * What AsyncLoaderSystem does, without a frame; cheap on an empty queue, so pollable.
 *
 * @param resources Holds the assets the completions name.
 */
void finalizeAsyncLoads(ResourceManager& resources);

/**
 * @brief Finalise completions until nothing in @p resources is still loading.
 *
 * Completions for another manager's assets go back on the process-wide queue rather
 * than being dropped. Gives up after @p patience with no load landing, so a dead
 * worker cannot hang a build silently; the bound is on silence, not the whole wait.
 *
 * @param resources Holds the assets being waited on.
 * @param patience  How long to wait with nothing landing; generous, for the largest import.
 * @return False when assets were still loading at the deadline (logged).
 */
bool awaitAsyncLoads(
    ResourceManager& resources,
    std::chrono::milliseconds patience = std::chrono::seconds(30)
);

/**
 * @brief Drains the AsyncLoadQueue once per frame and finalises completed asset loads.
 *
 * Runs at Simulation, ahead of Render, so a resource is uploaded in the frame it lands.
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
