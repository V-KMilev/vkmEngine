#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief One live particle. Runtime state owned by its emitter - never serialized.
 */
struct Particle {
    glm::vec3 position{0.0f};  ///< World position.
    glm::vec3 velocity{0.0f};  ///< World velocity.
    float     age      = 0.0f; ///< Seconds since spawn.
    float     lifetime = 1.0f; ///< Seconds this particle lives.
};

/**
 * @brief CPU-simulated particle emitter - muzzle flashes, impacts, smoke puffs.
 *
 * Spawns particles at the entity's world position at `rate` per second, each
 * living `lifetime` seconds while integrating velocity + acceleration. Size and
 * colour lerp from start to end across that life, so a spark can fade and shrink
 * without any per-particle authoring.
 *
 * Simulated by ParticleSystem in the Simulation stage; drawn as camera-facing
 * billboards by the backend. The live `particles` array is runtime state and is
 * deliberately outside the reflected (serialized) field list.
 */
struct ParticleEmitter {
    bool     emitting     = true;
    float    rate         = 30.0f;  ///< Particles spawned per second.
    float    lifetime     = 1.5f;   ///< Seconds each particle lives.
    uint32_t maxParticles = 256;    ///< Hard cap on simultaneously live particles.

    glm::vec3 velocity{0.0f, 2.0f, 0.0f};      ///< Initial velocity (world).
    float     spread = 1.0f;                    ///< Random velocity spread added per axis.
    glm::vec3 acceleration{0.0f, -2.0f, 0.0f};  ///< Constant acceleration (gravity, drift).

    // Look (lerped start -> end over each particle's life)
    glm::vec4 startColor{1.0f, 0.8f, 0.4f, 1.0f};
    glm::vec4 endColor{1.0f, 0.2f, 0.0f, 0.0f};
    float     startSize = 0.25f;
    float     endSize   = 0.02f;
    float     softness  = 1.0f;  ///< Edge falloff: 1 = fully soft blob, 0 = hard-edged crisp disc.
    bool      additive  = true;  ///< Additive blend (sparks/fire) vs alpha (smoke).

    // Runtime state - not serialized.
    std::vector<Particle> particles;
    float                 spawnAccumulator = 0.0f;
};

VKM_REFLECT_BEGIN(ParticleEmitter)
    VKM_F(emitting),
    VKM_F(rate),
    VKM_F(lifetime),
    VKM_F(maxParticles),
    VKM_F(velocity),
    VKM_F(spread),
    VKM_F(acceleration),
    VKM_F(startColor),
    VKM_F(endColor),
    VKM_F(startSize),
    VKM_F(endSize),
    VKM_F(softness),
    VKM_F(additive)
VKM_REFLECT_END()

} // namespace Engine
