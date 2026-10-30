#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace Vkm::Engine {

class ResourceManager;
class Scene;

constexpr std::chrono::milliseconds ASYNC_LOAD_PATIENCE = std::chrono::seconds(30);
    ///< How long loads may go with none landing before a waiter takes them for lost.

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
 * @brief Whether the first bakes of a world just arrived still wait: on its meshes and textures
 *        (assetsLoading), and on the sky's image the backend decodes.
 *
 * A world arriving (the scene's epoch moving) is captured whole or not at all: a probe or a volume
 * baked while an asset decodes would picture a world part-made, and none is taken again when it lands.
 * The frame a world arrives always waits, as the backend says what it decodes for that world only
 * once it has drawn it. A load decoding once the world's first bakes have begun holds nothing. None
 * landing for the patience lets it go, so a lost decode cannot hold the bakes for good.
 */
class ArrivalHold {
    public:
        /**
         * @brief A hold that takes loads for lost after @p patience with none landing.
         *
         * @param patience ASYNC_LOAD_PATIENCE unless a test needs less.
         */
        explicit ArrivalHold(std::chrono::milliseconds patience = ASYNC_LOAD_PATIENCE)
            : m_patience(patience) {}
        ~ArrivalHold() = default;

        ArrivalHold(const ArrivalHold& other) = default;
        ArrivalHold& operator=(const ArrivalHold& other) = default;

        ArrivalHold(ArrivalHold && other) = default;
        ArrivalHold& operator=(ArrivalHold && other) = default;

    public:
        /**
         * @brief Whether the world's first bakes wait; asked once a frame.
         *
         * @param scene       The world; its epoch says when one arrives.
         * @param resources   Holds its assets.
         * @param skyDecoding Whether the backend's last frame left the sky's image decoding
         *                    (RenderReadiness::skyPending).
         * @return True while they wait.
         */
        bool holds(const Scene& scene, const ResourceManager& resources, bool skyDecoding);

    private:
        std::chrono::milliseconds m_patience;

        uint64_t m_epoch   = 0;
        bool     m_seen    = false;  ///< m_epoch holds a world's.
        bool     m_waiting = false;

        size_t                                m_left = 0;  ///< Loads decoding, as last counted.
        std::chrono::steady_clock::time_point m_since{};   ///< When m_left last fell.
};

} // namespace Vkm::Engine
