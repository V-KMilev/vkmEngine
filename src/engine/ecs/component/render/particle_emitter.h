#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief CPU-simulated particle emitter - muzzle flashes, impacts, smoke puffs.
 *
 * Spawns at the entity's world position; size and colour lerp start -> end over
 * each particle's life. Authored data only: live particles are ParticleSystem's
 * (LiveParticles).
 */
struct ParticleEmitter {
    bool     emitting     = true;
    float    rate         = 30.0f;  ///< Per second.
    float    lifetime     = 1.5f;   ///< Seconds.
    uint32_t maxParticles = 256;    ///< Cap on live particles.

    glm::vec3 velocity{0.0f, 2.0f, 0.0f};       ///< Initial, world space.
    float     spread = 1.0f;                    ///< Random velocity added per axis.
    glm::vec3 acceleration{0.0f, -2.0f, 0.0f};

    // Lerped start -> end over each particle's life
    glm::vec4 startColor{1.0f, 0.8f, 0.4f, 1.0f};
    glm::vec4 endColor{1.0f, 0.2f, 0.0f, 0.0f};
    float     startSize = 0.25f;
    float     endSize   = 0.02f;
    float     softness  = 1.0f;  ///< 1 = soft blob, 0 = hard disc.
    bool      additive  = true;  ///< Additive (sparks/fire) vs alpha (smoke).
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::ParticleEmitter)
    VKM_F(emitting)
    VKM_F(rate)
    VKM_F(lifetime)
    VKM_F(maxParticles)
    VKM_F(velocity)
    VKM_F(spread)
    VKM_F(acceleration)
    VKM_F(startColor)
    VKM_F(endColor)
    VKM_F(startSize)
    VKM_F(endSize)
    VKM_F(softness)
    VKM_F(additive)
VKM_REFLECT_END()
