#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "core/system.h"

namespace Vkm::Engine {

class ResourceManager;
class Scene;

/// How long loads may go with none landing before a waiter takes them for lost.
constexpr std::chrono::milliseconds ASYNC_LOAD_PATIENCE = std::chrono::seconds(30);

/**
 * @brief Finalise every completion waiting in the AsyncLoadQueue against @p resources.
 *
 * What AsyncLoaderSystem does, without a frame; cheap on an empty queue, so pollable.
 *
 * @param resources Holds the assets the completions name.
 */
void finalizeAsyncLoads(ResourceManager& resources);

/**
 * @brief How many assets of @p resources are still decoding: meshes and textures loading.
 *
 * @param resources Graph to count in.
 * @return The count; 0 once every load has landed.
 */
size_t assetsLoading(const ResourceManager& resources);

/**
 * @brief How many meshes of @p resources are still decoding.
 *
 * @param resources Graph to count in.
 * @return The count; 0 once every mesh load has landed.
 */
size_t meshesLoading(const ResourceManager& resources);

/**
 * @brief Finalise completions until nothing in @p resources is still loading.
 *
 * Completions for another manager's assets go back on the process-wide queue rather
 * than being dropped. Gives up after @p patience with no load landing, so a dead
 * worker cannot hang a build silently; the bound is on silence, not the whole wait.
 *
 * @param resources Holds the assets being waited on.
 * @param patience  How long to wait with nothing landing; generous, for the largest import.
 * @return False when assets were still loading at the deadline, or one came out empty (logged).
 */
bool awaitAsyncLoads(ResourceManager& resources, std::chrono::milliseconds patience = ASYNC_LOAD_PATIENCE);

/**
 * @brief Whether a world just arrived still waits on what it counts: its meshes for its ticks
 *        (meshesLoading), its meshes and textures for its first bakes (assetsLoading).
 *
 * A world arriving (the scene's epoch moving) runs no tick while a mesh decodes: until it lands,
 * a collider or a water surface made of it is not there, and a body ticked meanwhile falls through
 * the floor it stands on. A load decoding once the world has run - streamed in - holds nothing.
 * None landing for the patience lets it go, so a lost decode cannot stop the world for good.
 */
class ArrivalHold {
    public:
        /// How many of what an arrival waits on are still decoding.
        using Loading = size_t (*)(const ResourceManager&);

        /**
         * @brief A hold that takes loads for lost after @p patience with none landing.
         *
         * @param patience ASYNC_LOAD_PATIENCE unless a test needs less.
         * @param loading  What it waits on: meshesLoading for the ticks, assetsLoading for bakes.
         * @param goesOn   What its error says goes on without them: "its ticks run", say.
         */
        explicit ArrivalHold(
            std::chrono::milliseconds patience = ASYNC_LOAD_PATIENCE,
            Loading loading = meshesLoading,
            const char* goesOn = "its ticks run"
        )
            : m_patience(patience)
            , m_loading(loading)
            , m_goesOn(goesOn) {}
        ~ArrivalHold() = default;

        ArrivalHold(const ArrivalHold& other) = default;
        ArrivalHold& operator=(const ArrivalHold& other) = default;

        ArrivalHold(ArrivalHold && other) = default;
        ArrivalHold& operator=(ArrivalHold && other) = default;

    public:
        /**
         * @brief Whether the world waits; asked once a frame.
         *
         * @param scene     The world; its epoch says when one arrives.
         * @param resources Holds what it waits on.
         * @return True while it waits.
         */
        bool holds(const Scene& scene, const ResourceManager& resources);

    private:
        std::chrono::milliseconds m_patience;
        Loading                   m_loading;
        const char*               m_goesOn;

        uint64_t m_epoch   = 0;
        bool     m_seen    = false;  ///< m_epoch holds a world's.
        bool     m_waiting = false;

        size_t                                m_left = 0;  ///< Loads decoding, as last counted.
        std::chrono::steady_clock::time_point m_since{};   ///< When m_left last fell.
};

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
