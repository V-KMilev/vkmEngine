#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "core/reflect.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

/**
 * @brief CPU-simulated particle emitter - muzzle flashes, impacts, smoke puffs.
 *
 * Spawns at the entity's world position, or anywhere in a box round it; size and
 * colour lerp start -> end over each particle's life. Each draws a soft disc, or its
 * `texture` - a frame of it, read as a sheet - and may streak along its velocity or
 * spin. Authored data, `burst` too, which the system spends: live particles are
 * ParticleSystem's (LiveParticles).
 *
 * A one-off puff is an entity - a prefab, say - with `emitting` off, a `burst`, and
 * `destroyWhenSpent`: it fires once, and goes once its last particle has.
 */
struct ParticleEmitter {
    bool     emitting     = true;
    float    rate         = 30.0f;  ///< Per second.
    float    lifetime     = 1.5f;   ///< Seconds.
    uint32_t maxParticles = 256;    ///< Cap on live particles.

    /// Particles to spawn at once on the next update, besides the rate; set again to fire again.
    uint32_t burst = 0;

    /// Half the size of the box, world axes, about the entity, particles are born in; zero is a point.
    glm::vec3 halfExtents{0.0f, 0.0f, 0.0f};

    /// Destroy the entity, and everything under it, once it has fired, neither emits nor holds a
    /// burst, and its last particle has died; one waiting to be fired stays.
    bool destroyWhenSpent = false;

    glm::vec3 velocity{0.0f, 2.0f, 0.0f};       ///< Initial, world space.
    float     spread = 1.0f;                    ///< Random velocity added per axis.
    glm::vec3 acceleration{0.0f, -2.0f, 0.0f};

    // Lerped start -> end over each particle's life
    glm::vec4 startColor{1.0f, 0.8f, 0.4f, 1.0f};
    glm::vec4 endColor{1.0f, 0.2f, 0.0f, 0.0f};
    float     startSize = 0.25f;
    float     endSize   = 0.02f;
    float     softness  = 1.0f;  ///< 1 = soft blob, 0 = hard disc. Unread with a texture.
    bool      additive  = true;  ///< Additive (sparks/fire) vs alpha (smoke).

    /// What each particle shows, tinted by its colour; none draws the soft disc.
    TextureHandle texture;

    /// The texture as a sheet of frames, columns by rows, read left to right from the top.
    uint32_t sheetColumns = 1;
    uint32_t sheetRows    = 1;
    float    sheetFps     = 0.0f;   ///< Frames a second, looping; 0 plays the sheet once over a life.
    bool     randomFrame  = false;  ///< Each particle starts on a frame of its own.

    /// Seconds of its travel a particle streaks behind it, along its velocity; 0 is a square.
    float stretch = 0.0f;

    float spin           = 0.0f;   ///< Radians a second each turns, about the view. Unread when streaking.
    bool  randomRotation = false;  ///< Each particle starts turned at an angle of its own.
};

/**
 * @brief How many frames @p emitter's sheet holds: its columns by its rows, each at least one.
 *
 * @param emitter The emitter.
 * @return The frame count, at least 1.
 */
inline uint32_t sheetFrames(const ParticleEmitter& emitter) {
    return std::max(emitter.sheetColumns, 1u) * std::max(emitter.sheetRows, 1u);
}

/**
 * @brief The sheet frame a particle shows at @p age: once through over its life, or at sheetFps.
 *
 * @param emitter  The emitter it came from.
 * @param age      Seconds since it was born.
 * @param lifetime Seconds it lives.
 * @param start    The frame it was born on.
 * @return The frame, 0 .. sheetFrames(emitter) - 1.
 */
inline uint32_t sheetFrameAt(const ParticleEmitter& emitter, float age, float lifetime, uint32_t start) {
    const uint32_t frames = sheetFrames(emitter);
    const auto     count  = static_cast<float>(frames);
    uint32_t       ahead  = 0;
    if (emitter.sheetFps > 0.0f) {
        ahead = static_cast<uint32_t>(std::fmod(std::max(age * emitter.sheetFps, 0.0f), count));
    } else {
        // Held on the last frame at the very end of a life, where the share reaches one.
        const float share = lifetime > 0.0f ? std::clamp(age / lifetime, 0.0f, 1.0f) : 1.0f;
        ahead = std::min(static_cast<uint32_t>(share * count), frames - 1);
    }
    return (start + std::min(ahead, frames - 1)) % frames;
}

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::ParticleEmitter)
    VKM_F(emitting)
    VKM_F(rate)
    VKM_F(lifetime)
    VKM_F(maxParticles)
    VKM_F(burst)
    VKM_F(halfExtents)
    VKM_F(destroyWhenSpent)
    VKM_F(velocity)
    VKM_F(spread)
    VKM_F(acceleration)
    VKM_F(startColor)
    VKM_F(endColor)
    VKM_F(startSize)
    VKM_F(endSize)
    VKM_F(softness)
    VKM_F(additive)
    VKM_F(texture)
    VKM_F(sheetColumns)
    VKM_F(sheetRows)
    VKM_F(sheetFps)
    VKM_F(randomFrame)
    VKM_F(stretch)
    VKM_F(spin)
    VKM_F(randomRotation)
VKM_REFLECT_END()
