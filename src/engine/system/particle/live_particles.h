#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief One live particle.
 */
struct Particle {
    glm::vec3 position{0.0f};  ///< World space.
    glm::vec3 velocity{0.0f};  ///< World space.
    float     age      = 0.0f; ///< Seconds since spawn.
    float     lifetime = 1.0f; ///< Seconds.
};

/**
 * @brief Every emitter's live particles: ParticleSystem's per-frame product.
 *
 * Kept off the ParticleEmitter component, which travels with undo snapshots,
 * duplicates and the play-mode snapshot: a pool there would make each a bulk
 * copy, and an undo would rewind the particles.
 */
struct LiveParticles {
    struct Pool {
        EntityId              emitter;
        std::vector<Particle> particles;
        float                 spawnAccumulator = 0.0f;  ///< Fractional spawns carried to the next frame.
        uint64_t              seen = 0;                 ///< Last simulation step that found its emitter.
    };

    std::unordered_map<uint32_t, Pool> pools;  ///< By emitter entity slot.

    /**
     * @brief The particles @p emitter has alive, or null when it has none.
     *
     * @param emitter An entity carrying a ParticleEmitter.
     * @return Its pool's particles, or null.
     */
    const std::vector<Particle>* of(EntityId emitter) const {
        const auto found = pools.find(emitter.slot());
        if (found == pools.end() || found->second.emitter != emitter) return nullptr;
        return &found->second.particles;
    }
};

} // namespace Vkm::Engine
