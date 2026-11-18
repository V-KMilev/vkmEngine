#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/frame/arrival_hold.h"

namespace Vkm::Engine {

class ResourceManager;
class Scene;

/**
 * @brief Whether the world's ticks wait: while a mesh a Collider or a Water of it needs is still
 *        decoding, as the world arrives or at any time after.
 *
 * Until the mesh lands, a collider or a water surface made of it is not there, and a body ticked
 * meanwhile falls through it; held, what is made of it takes part from the tick after it is made, on
 * any disk. A mesh only drawn holds nothing. None of them landing for the patience lets the world go
 * on without them, and the error says how many; a mesh starting to decode after that holds it again.
 * Costs nothing while no mesh of the graph decodes.
 */
class TickHold {
    public:
        /**
         * @brief A hold that takes the meshes it waits on for lost after @p patience with none landing.
         *
         * @param patience ASYNC_LOAD_PATIENCE unless a test needs less.
         */
        explicit TickHold(std::chrono::milliseconds patience = ASYNC_LOAD_PATIENCE) : m_patience(patience) {}
        ~TickHold() = default;

        TickHold(const TickHold& other) = default;
        TickHold& operator=(const TickHold& other) = default;

        TickHold(TickHold && other) = default;
        TickHold& operator=(TickHold && other) = default;

    public:
        /**
         * @brief Whether the ticks wait; asked before the frame's ticks and after each.
         *
         * @param scene     The world, whose colliders and water name their meshes.
         * @param resources Holds the meshes.
         * @return True while one it needs decodes.
         */
        bool holds(const Scene& scene, const ResourceManager& resources);

    private:
        /**
         * @brief What says the graph's meshes have changed: a decode begins only as its stub is added.
         */
        struct Meshes {
            uint64_t epoch    = 0;  ///< ResourceManager::epoch; never 0 in a graph.
            size_t   count    = 0;  ///< How many meshes it holds.
            uint64_t revision = 0;  ///< ResourceManager::revisionOf for meshes.

            bool operator==(const Meshes& other) const {
                return epoch == other.epoch && count == other.count && revision == other.revision;
            }
        };

        /**
         * @brief Gather into m_needed, once each, the uid of every mesh a collider or a water of @p scene
         *        needs that is still decoding and not given up on.
         */
        void gatherNeeded(const Scene& scene, const ResourceManager& resources);

    private:
        std::chrono::milliseconds m_patience;

        Meshes m_still;  ///< The graph's meshes when none was last seen decoding; none is while they match.

        std::vector<uint64_t> m_needed;  ///< Scratch: the meshes waited on this time.
        std::vector<uint64_t> m_lost;    ///< Meshes, by uid, the patience ran out on.

        bool                                  m_waiting = false;
        size_t                                m_left    = 0;  ///< Meshes waited on, as last counted.
        std::chrono::steady_clock::time_point m_since{};      ///< When the wait began or m_left last fell.
};

} // namespace Vkm::Engine
