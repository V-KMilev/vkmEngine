#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "core/reflect.h"
#include "core/math/curve.h"
#include "ecs/component/render/mesh.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

/**
 * @brief Where an emitter's particles are born, and which way they leave, in its own frame.
 */
enum class EmitShape : uint8_t {
    Point,       ///< At its origin, leaving every way.
    Sphere,      ///< In a ball of `radius`, leaving outward.
    Hemisphere,  ///< In the ball's half above its XZ plane, leaving outward.
    Cone,        ///< On a disc of `radius`, leaving up +Y within `angle` of it.
    Box,         ///< In a box of `halfExtents`, leaving up +Y within `angle` of it.
    Disc,        ///< On a disc of `radius` in its XZ plane, leaving outward in that plane.
    Count        ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief How a particle's colour reaches the frame.
 */
enum class ParticleBlend : uint8_t {
    Additive,    ///< It emits its colour - sparks, fire, glows - and feeds bloom past 1.
    Alpha,       ///< Its colour is an albedo, lit by the scene's lights and shadows - smoke, dust.
    Distortion,  ///< It bends the scene behind it - heat haze, shockwaves - and adds no colour.
    Count        ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief What each particle draws as.
 */
enum class ParticleRender : uint8_t {
    Billboard,  ///< A card facing the view, turned by its rotation.
    Stretched,  ///< A card along its velocity, `stretch` seconds of its travel long.
    Ribbon,     ///< One strip through the emitter's particles in the order they were born.
    Trail,      ///< A strip behind each particle through where it was over `trailLength` seconds.
    Count       ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief When a ParticleTrigger fires.
 */
enum class ParticleEvent : uint8_t {
    Birth,      ///< A particle of the parent is born.
    Death,      ///< A particle of the parent dies, of age or of a strike.
    Collision,  ///< A particle of the parent strikes a body, as ParticleEmitter::collide counts a strike.
    Count       ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief CPU-simulated particle emitter: fire, smoke, sparks, impacts, trails, heat haze.
 *
 * Each particle is born on the emitter's `shape`, in its frame, with what its ranges draw; its
 * size and colour then follow their curves over its life, forces move it, and it may strike the
 * physics world. It draws as `render` says, blended as `blend` says, as a soft disc or a frame of
 * its `texture` read as a sheet. Authored data, `burst` too, which the system spends: live
 * particles are ParticleSystem's (LiveParticles).
 *
 * A one-off puff is an entity - a prefab, say - with `emitting` off, a `burst`, and
 * `destroyWhenSpent`: it fires once, and goes once its last particle has. A child entity with a
 * ParticleEmitter and a ParticleTrigger is a sub-emitter: it spawns where the parent's particles
 * are born, die or strike.
 */
struct ParticleEmitter {
    bool     emitting     = true;
    float    rate         = 30.0f;  ///< Particles a second.
    float    ratePerMetre = 0.0f;   ///< Particles a metre the emitter travels, besides the rate.
    uint32_t maxParticles = 256;    ///< Cap on live particles.

    /// Particles to spawn at once on the next update, besides the rates; set again to fire again.
    uint32_t burst = 0;

    /// Destroy the entity, and everything under it, once it has fired, neither emits nor holds a
    /// burst, and its last particle has died; one waiting to be fired stays.
    bool destroyWhenSpent = false;

    /// Where particles are born and which way they leave, in the emitter's frame.
    EmitShape shape       = EmitShape::Cone;
    float     radius      = 0.0f;   ///< Sphere, Hemisphere, Cone and Disc, metres.
    float     angle       = glm::radians(25.0f);  ///< Cone and Box: radians off +Y a particle may leave at.
    glm::vec3 halfExtents{0.5f};    ///< Box, metres.
    bool      fromSurface = false;  ///< Born on the shape's surface or rim, not inside it.

    Math::FloatRange lifetime{1.5f, 1.5f};  ///< Seconds.
    Math::FloatRange speed{2.0f, 2.0f};     ///< Metres a second, the way the shape sends it.
    /// The share of its source's velocity it takes: the emitter's own, or a sub-emitter's parent
    /// particle's.
    float     inheritVelocity = 0.0f;
    glm::vec3 acceleration{0.0f, -2.0f, 0.0f};  ///< World space, metres a second squared: gravity.
    float     drag            = 0.0f;       ///< Velocity lost a second: it keeps exp(-drag) of it.
    float     turbulence      = 0.0f;       ///< Scales the swirl it accelerates by (ParticleSystem::swirlAt).
    float     turbulenceScale = 1.0f;       ///< Metres the swirls span.

    Math::FloatRange size{0.25f, 0.25f};    ///< Half the card's width, metres, drawn at birth.
    Math::Curve      sizeOverLife{{{0.0f, 1.0f}, {1.0f, 0.08f}}};  ///< Multiplies the drawn size.
    Math::Gradient   color{{{0.0f, {1.0f, 0.8f, 0.4f, 1.0f}}, {1.0f, {1.0f, 0.2f, 0.0f, 0.0f}}}};
    float            intensity = 1.0f;      ///< Multiplies the colour: past 1, an emitting one blooms.
    Math::FloatRange rotation{0.0f, 0.0f};  ///< Radians it is born turned by, about the view.
    Math::FloatRange spin{0.0f, 0.0f};      ///< Radians a second it turns.
    float            softness = 1.0f;       ///< The disc's edge, 1 soft to 0 hard. Unread with a texture.

    ParticleBlend blend      = ParticleBlend::Additive;
    float         emission   = 0.0f;   ///< Alpha: the share of its colour it gives off - fire in smoke.
    float         distortion = 0.03f;  ///< Distortion: how far it bends what is behind, in view heights.

    /// A point light its live particles give off together, at their middle: this much intensity for
    /// each one at full alpha, summed by its colour's alpha, so it fades and dies as they do - a
    /// fireball's flash, a fire's glow. 0 gives none. It casts no shadow, and no capture bakes it: a
    /// fire the probes should see is a flickering Light.
    float     lightIntensity = 0.0f;
    glm::vec3 lightColor{1.0f, 0.6f, 0.3f};  ///< The light's colour.
    float     lightRadius = 8.0f;            ///< Metres the light reaches.

    ParticleRender render      = ParticleRender::Billboard;
    float          stretch     = 0.05f;  ///< Stretched: seconds of its travel the card spans.
    float          trailLength = 0.3f;   ///< Trail: seconds of where it was the strip spans.

    /// What each particle shows, tinted by its colour; none draws the soft disc. A Distortion
    /// particle reads it as a normal map, so import it with TextureUsage::Normal.
    TextureHandle texture;

    /// Alpha: a normal map laid over the sheet frame for frame, lighting each puff by its own folds
    /// rather than as a ball.
    TextureHandle normalMap;

    /// The texture as a sheet of frames, columns by rows, read left to right from the top.
    uint32_t sheetColumns = 1;
    uint32_t sheetRows    = 1;
    float    sheetFps     = 0.0f;   ///< Frames a second, looping; 0 plays the sheet once over a life.
    bool     randomFrame  = false;  ///< Each particle starts on a frame of its own.
    bool     sheetBlend   = false;  ///< Each frame fades into the next: a slow flipbook moves smoothly.

    /// Strike the physics world's bodies along each step. A strike counts - for lifeLostOnHit
    /// and a Collision trigger - only when it comes in faster than settling would bring it.
    bool  collide       = false;
    float bounce        = 0.3f;   ///< The share of its speed into the surface it keeps, turned back.
    float lifeLostOnHit = 0.0f;   ///< The share of its whole life a strike takes; 1 ends it.

    /// Who draws its particles, as for a Mesh. Particles cast no shadow and no probe captures them,
    /// and a planar reflection draws those the eye does, less the viewmodel layer, so through a
    /// scene camera AllButEye draws none and EyeOnly draws as All; Viewmodel lines a muzzle flash
    /// up with the weapon in hand and shows it in front of it.
    SeenBy seenBy = SeenBy::All;
};

/**
 * @brief Makes a child entity's ParticleEmitter a sub-emitter of its parent's.
 *
 * Each time a parent particle meets `event`, `count` particles are born where it is, with the
 * child's own ranges and curves, leaving as the child's shape sends them plus the child's
 * `inheritVelocity` of the parent particle's velocity. A ParticleEmitter with a trigger never emits
 * of its own: its `emitting`, rates, burst and destroyWhenSpent go unread. With no emitter parent
 * it emits nothing at all.
 */
struct ParticleTrigger {
    ParticleEvent event = ParticleEvent::Death;
    uint32_t      count = 4;
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
 * @brief Where in its sheet a particle is: the frame it shows, and how far it has faded into the next.
 */
struct SheetFrame {
    uint32_t frame = 0;     ///< The frame it shows.
    uint32_t next  = 0;     ///< The frame after it.
    float    blend = 0.0f;  ///< How far it has faded into next, 0..1; 0 unless sheetBlend.
};

/**
 * @brief Where in the sheet a particle is at @p age: once through over its life, or at sheetFps.
 *
 * Cross-faded, a life ends on the last frame, not part way into it.
 *
 * @param emitter  The emitter it came from.
 * @param age      Seconds since it was born.
 * @param lifetime Seconds it lives.
 * @param start    The frame it was born on.
 * @return The frames, 0 .. sheetFrames(emitter) - 1, and the fade between them.
 */
inline SheetFrame sheetFrameAt(const ParticleEmitter& emitter, float age, float lifetime, uint32_t start) {
    const uint32_t frames = sheetFrames(emitter);
    const auto     count  = static_cast<float>(frames);
    const bool     loops  = emitter.sheetFps > 0.0f;
    float position = 0.0f;
    if (loops) {
        position = std::fmod(std::max(age * emitter.sheetFps, 0.0f), count);
    } else {
        const float share = lifetime > 0.0f ? std::clamp(age / lifetime, 0.0f, 1.0f) : 1.0f;
        position = share * (emitter.sheetBlend ? count - 1.0f : count);
    }
    // Held on the last frame at the very end of a life, where the share reaches one.
    const uint32_t ahead = std::min(static_cast<uint32_t>(position), frames - 1);
    const uint32_t after = loops ? (ahead + 1) % frames : std::min(ahead + 1, frames - 1);

    SheetFrame sheet;
    sheet.frame = (start + ahead) % frames;
    sheet.next  = (start + after) % frames;
    sheet.blend = emitter.sheetBlend && after != ahead ? position - static_cast<float>(ahead) : 0.0f;
    return sheet;
}

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::EmitShape, "Point", "Sphere", "Hemisphere", "Cone", "Box", "Disc")
VKM_ENUM_NAMES(::Vkm::Engine::ParticleBlend, "Additive", "Alpha", "Distortion")
VKM_ENUM_NAMES(::Vkm::Engine::ParticleRender, "Billboard", "Stretched", "Ribbon", "Trail")
VKM_ENUM_NAMES(::Vkm::Engine::ParticleEvent, "Birth", "Death", "Collision")

VKM_REFLECT_BEGIN(::Vkm::Engine::ParticleEmitter)
    VKM_F(emitting)
    VKM_F(rate)
    VKM_F(ratePerMetre)
    VKM_F(maxParticles)
    VKM_F(burst)
    VKM_F(destroyWhenSpent)
    VKM_F(shape)
    VKM_F(radius)
    VKM_F(angle)
    VKM_F(halfExtents)
    VKM_F(fromSurface)
    VKM_F(lifetime)
    VKM_F(speed)
    VKM_F(inheritVelocity)
    VKM_F(acceleration)
    VKM_F(drag)
    VKM_F(turbulence)
    VKM_F(turbulenceScale)
    VKM_F(size)
    VKM_F(sizeOverLife)
    VKM_F(color)
    VKM_F(intensity)
    VKM_F(rotation)
    VKM_F(spin)
    VKM_F(softness)
    VKM_F(blend)
    VKM_F(emission)
    VKM_F(distortion)
    VKM_F(lightIntensity)
    VKM_F(lightColor)
    VKM_F(lightRadius)
    VKM_F(render)
    VKM_F(stretch)
    VKM_F(trailLength)
    VKM_F(texture)
    VKM_F(normalMap)
    VKM_F(sheetColumns)
    VKM_F(sheetRows)
    VKM_F(sheetFps)
    VKM_F(randomFrame)
    VKM_F(sheetBlend)
    VKM_F(collide)
    VKM_F(bounce)
    VKM_F(lifeLostOnHit)
    VKM_F(seenBy)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::ParticleTrigger)
    VKM_F(event)
    VKM_F(count)
VKM_REFLECT_END()
