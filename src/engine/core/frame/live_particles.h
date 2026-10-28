#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief One live particle, with what its emitter's ranges drew for it at birth.
 */
struct Particle {
    glm::vec3 position{0.0f};    ///< World space.
    glm::vec3 velocity{0.0f};    ///< World space.
    float     age        = 0.0f; ///< Seconds since spawn.
    float     lifetime   = 1.0f; ///< Seconds.
    float     size       = 0.0f; ///< Half its width at birth; the size curve scales it over its life.
    float     angle      = 0.0f; ///< Radians it was born turned by.
    float     spin       = 0.0f; ///< Radians a second it turns.
    uint32_t  frame      = 0;    ///< The sheet frame it was born on (ParticleEmitter::randomFrame).
    float     trailClock = 0.0f; ///< Seconds since its trail last took a point.
    uint32_t  trailCount = 0;    ///< Points its trail holds, newest first.
};

/**
 * @brief Every emitter's live particles: ParticleSystem's per-frame product.
 *
 * Kept off the ParticleEmitter component, which travels with undo snapshots,
 * duplicates and the play-mode snapshot: a pool there would make each a bulk
 * copy, and an undo would rewind the particles.
 */
struct LiveParticles {
    /// Points a particle's trail keeps, its own position aside.
    static constexpr uint32_t TRAIL_POINTS = 12;

    struct Pool {
        EntityId              emitter;
        std::vector<Particle> particles;  ///< In the order they were born, oldest first.

        /// A Trail emitter's history: TRAIL_POINTS a particle, beside particles, newest first.
        std::vector<glm::vec3> trails;

        float     spawnAccumulator = 0.0f;  ///< Fractional spawns carried to the next frame.
        float     metreAccumulator = 0.0f;  ///< The same for spawns by distance.
        glm::vec3 lastOrigin{0.0f};         ///< Where the emitter was the step before.
        bool      placed = false;           ///< Whether lastOrigin holds anything yet.
        bool      fired  = false;           ///< Whether it has fired: spawned, or been handed a burst.
        uint64_t  seen   = 0;               ///< Last simulation step that found its emitter.

        /**
         * @brief The trail points of particle @p index, newest first.
         *
         * @param index Its place in particles; a Trail emitter's pool keeps a trail for each.
         * @return Its first point; particles[index].trailCount of them are filled.
         */
        const glm::vec3* trailOf(size_t index) const { return trails.data() + index * TRAIL_POINTS; }
    };

    std::unordered_map<uint32_t, Pool> pools;  ///< By emitter entity slot.

    /**
     * @brief The pool of @p emitter, or null when it has none.
     *
     * @param emitter An entity carrying a ParticleEmitter.
     * @return Its pool, or null.
     */
    const Pool* poolOf(EntityId emitter) const {
        const auto found = pools.find(emitter.slot());
        if (found == pools.end() || found->second.emitter != emitter) return nullptr;
        return &found->second;
    }
};

} // namespace Vkm::Engine
