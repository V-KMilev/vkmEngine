#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"
#include "base/math/curve.h"
#include "base/math/float_range.h"
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
 * @brief How a particle's color reaches the frame.
 */
enum class ParticleBlend : uint8_t {
    Additive,    ///< It emits its color - sparks, fire, glows - and feeds bloom past 1.
    Alpha,       ///< Its color is an albedo, lit by the scene's lights and shadows - smoke, dust.
    Distortion,  ///< It bends the scene behind it - heat haze, shockwaves - and adds no color.
    Count        ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief What each particle draws as.
 */
enum class ParticleRender : uint8_t {
    Billboard,  ///< A card facing the view, turned by its rotation.
    Stretched,  ///< A card along its velocity, `stretch` seconds of its travel long.
    Trail,      ///< A strip behind each particle through where it was over `trailLength` seconds.
    Count       ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief CPU-simulated particle emitter: fire, smoke, sparks, impacts, trails, heat haze.
 *
 * Each particle is born on the emitter's `shape`, in its frame, with what its ranges draw; its
 * size and color then follow their curves over its life, forces move it, and it may strike the
 * physics world. It draws as `render` says, blended as `blend` says, as a soft disc or a frame of
 * its `texture` read as a sheet. Authored data, `burst` too, which the system spends: live
 * particles are ParticleSystem's (LiveParticles).
 *
 * A one-off puff is an entity - a prefab, say - with `emitting` off, a `burst`, and
 * `destroyWhenSpent`: it fires once, and goes once its last particle has.
 */
struct ParticleEmitter {
    bool     emitting     = true;
    float    rate         = 30.0f;  ///< Particles a second.
    float    ratePerMeter = 0.0f;   ///< Particles a meter the emitter travels, besides the rate.
    uint32_t maxParticles = 256;    ///< Cap on live particles.

    uint32_t burst = 0;  ///< Particles to spawn at once on the next update, besides the rates; set again to
                         ///< fire again.

    bool destroyWhenSpent = false;  ///< Destroy the entity, and everything under it, once it has fired,
                                    ///< neither emits nor holds a burst, and its last particle has died; one
                                    ///< waiting to be fired stays.

    EmitShape shape       = EmitShape::Cone;      ///< Where particles are born and which way they leave, in
                                                  ///< the emitter's frame.
    float     radius      = 0.0f;                 ///< Sphere, Hemisphere, Cone and Disc, meters.
    float     angle       = glm::radians(25.0f);  ///< Cone and Box: radians off +Y a particle may leave at.
    glm::vec3 halfExtents{0.5f};                  ///< Box, meters.
    bool      fromSurface = false;                ///< Born on the shape's surface or rim, not inside it.

    Math::FloatRange lifetime{1.5f, 1.5f};      ///< Seconds.
    Math::FloatRange speed{2.0f, 2.0f};         ///< Meters a second, the way the shape sends it.
    float     inheritVelocity = 0.0f;           ///< The share of the emitter's velocity it takes.
    glm::vec3 acceleration{0.0f, -2.0f, 0.0f};  ///< World space, meters a second squared: gravity.
    float     drag            = 0.0f;           ///< Velocity lost a second: it keeps exp(-drag) of it.
    float     turbulence      = 0.0f;           ///< Scales the swirl pushing it (ParticleSystem::swirlAt).
    float     turbulenceScale = 1.0f;           ///< Meters the swirls span.

    Math::FloatRange size{0.25f, 0.25f};    ///< Half the card's width, meters, drawn at birth.
    Math::Curve      sizeOverLife{{{0.0f, 1.0f}, {1.0f, 0.08f}}};  ///< Multiplies the drawn size.
    Math::Gradient   color{{{0.0f, {1.0f, 0.8f, 0.4f, 1.0f}}, {1.0f, {1.0f, 0.2f, 0.0f, 0.0f}}}};
    float            intensity = 1.0f;      ///< Multiplies the color: past 1, an emitting one blooms.
    Math::FloatRange rotation{0.0f, 0.0f};  ///< Radians it is born turned by, about the view.
    Math::FloatRange spin{0.0f, 0.0f};      ///< Radians a second it turns.
    float            softness = 1.0f;       ///< The disc's edge, 1 soft to 0 hard. Unread with a texture.

    ParticleBlend blend      = ParticleBlend::Additive;
    float         emission   = 0.0f;   ///< Alpha: the share of its color it gives off - fire in smoke.
    float         distortion = 0.03f;  ///< Distortion: how far it bends what is behind, in view heights.

    float     lightIntensity = 0.0f;         ///< A point light its live particles give off together, at their
                                             ///< middle: this much intensity for each at full alpha, summed
                                             ///< by its color's alpha, so it fades and dies as they do - a
                                             ///< fireball's flash, a fire's glow. 0 gives none. It casts no
                                             ///< shadow and no capture bakes it: a fire the probes should see
                                             ///< is a flickering Light.
    glm::vec3 lightColor{1.0f, 0.6f, 0.3f};  ///< The light's color.
    float     lightRadius    = 8.0f;         ///< Meters the light reaches.

    ParticleRender render      = ParticleRender::Billboard;
    float          stretch     = 0.05f;  ///< Stretched: seconds of its travel the card spans.
    float          trailLength = 0.3f;   ///< Trail: seconds of where it was the strip spans.

    TextureHandle texture;  ///< What each particle shows, tinted by its color; none draws the soft disc. A
                            ///< Distortion particle reads it as a normal map, so import it with
                            ///< TextureUsage::Normal.

    TextureHandle normalMap;  ///< Alpha: a normal map laid over the sheet frame for frame, lighting each puff
                              ///< by its own folds rather than as a ball.

    uint32_t sheetColumns = 1;      ///< The texture as a sheet of frames, columns by rows, read left to right
                                    ///< from the top; each read as 1..PARTICLE_SHEET_SIDE_MAX (sheetSide).
    uint32_t sheetRows    = 1;
    float    sheetFps     = 0.0f;   ///< Frames a second, looping; 0 plays the sheet once over a life.
    bool     randomFrame  = false;  ///< Each particle starts on a frame of its own.
    bool     sheetBlend   = false;  ///< Each frame fades into the next: a slow flipbook moves smoothly.

    bool  collide       = false;  ///< Strike the physics world's bodies along each step, never its own
                                  ///< entity's. A strike counts for lifeLostOnHit only when it comes in
                                  ///< faster than settling would bring it.
    float bounce        = 0.3f;   ///< The share of its speed into the surface it keeps, turned back.
    float lifeLostOnHit = 0.0f;   ///< The share of its whole life a strike takes; 1 ends it.

    SeenBy seenBy = SeenBy::All;  ///< Who draws its particles, as for a Mesh. Particles cast no shadow and no
                                  ///< probe captures them, so through a scene camera AllButEye draws none and
                                  ///< EyeOnly draws as All; Viewmodel lines a muzzle flash up with the weapon
                                  ///< in hand and shows it in front of it.
};

inline constexpr uint32_t PARTICLE_SHEET_SIDE_MAX = 64;  ///< The most columns, or rows, a particle sheet is
                                                         ///< read as having.

/**
 * @brief A sheet's columns or rows as they are read: a scene file can say any count.
 *
 * @param side sheetColumns or sheetRows.
 * @return It, within 1..PARTICLE_SHEET_SIDE_MAX.
 */
inline uint32_t sheetSide(uint32_t side) {
    return std::clamp(side, 1u, PARTICLE_SHEET_SIDE_MAX);
}

/**
 * @brief How many frames @p emitter's sheet holds: its columns by its rows, as sheetSide reads them.
 *
 * @param emitter The emitter.
 * @return The frame count, at least 1.
 */
inline uint32_t sheetFrames(const ParticleEmitter& emitter) {
    return sheetSide(emitter.sheetColumns) * sheetSide(emitter.sheetRows);
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
    const auto count = static_cast<float>(frames);
    const bool loops = emitter.sheetFps > 0.0f;
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
VKM_ENUM_NAMES(::Vkm::Engine::ParticleRender, "Billboard", "Stretched", "Trail")

VKM_REFLECT_BEGIN(::Vkm::Engine::ParticleEmitter)
    VKM_F(emitting)
    VKM_F(rate, 0.0f, 5000.0f, "Particles a second")
    VKM_F(ratePerMeter, 0.0f, 1000.0f, "Particles a meter it travels: a trail behind a mover")
    VKM_F(maxParticles, 1.0f, 50000.0f, "Most alive at once")
    VKM_F(burst, 0.0f, 50000.0f, "Spawned at once on the next update; set again to fire again")
    VKM_F(destroyWhenSpent, "Once spent, the entity and everything under it go")
    VKM_F(shape, "Where particles are born, and which way they leave")
    VKM_F(radius, 0.0f, 100.0f, "The shape's radius")
    VKM_F(angle, 0.0f, 3.1416f, "The widest off +Y one leaves; 0 sends each straight up")
    VKM_F(halfExtents, 0.0f, 100.0f, "The box's half sizes")
    VKM_F(fromSurface, "Born on the shape's surface or rim, not inside it")
    VKM_F(lifetime, 0.01f, 60.0f, "How long each lives, picked between the two")
    VKM_F(speed, 0.0f, 200.0f, "How fast each leaves, picked between the two")
    VKM_F(inheritVelocity, 0.0f, 1.0f, "The share of the emitter's own velocity it is born with")
    VKM_F(acceleration, -100.0f, 100.0f, "A world-space push on each, as gravity")
    VKM_F(drag, 0.0f, 50.0f, "It keeps exp(-drag) of its velocity a second")
    VKM_F(turbulence, 0.0f, 200.0f, "How hard the swirls push")
    VKM_F(turbulenceScale, 0.01f, 100.0f, "How far the swirls span")
    VKM_F(size, 0.0f, 50.0f, "Half a card's width at birth, picked between the two")
    VKM_F(sizeOverLife, 0.0f, 10.0f, "Multiplies the size it was born with")
    VKM_F(color, "Its color over its life; alpha fades it")
    VKM_F(intensity, 0.0f, 1000.0f, "Multiplies the color; past 1 an additive one blooms")
    VKM_F(rotation, -6.2832f, 6.2832f, "How far each is turned at birth")
    VKM_F(spin, -62.832f, 62.832f, "How fast each turns")
    VKM_F(softness, 0.0f, 1.0f, "The disc's edge: 1 soft, 0 hard")
    VKM_F(blend, "Additive for fire, Alpha for lit smoke, Distortion for heat haze")
    VKM_F(emission, 0.0f, 1.0f, "The share of its color it gives off")
    VKM_F(distortion, 0.0f, 0.5f, "How far it bends the view")
    VKM_F(lightIntensity, 0.0f, 10000.0f, "Light each live particle gives off; 0 none, no shadow")
    VKM_F(lightColor)
    VKM_F(lightRadius, 0.1f, 200.0f, "How far the light reaches")
    VKM_F(render, "A card facing the view, stretched along its travel, or a trail")
    VKM_F(stretch, 0.0f, 2.0f, "Seconds of its travel it spans")
    VKM_F(trailLength, 0.01f, 10.0f, "Seconds it trails")
    VKM_F(texture)
    VKM_F(normalMap)
    VKM_F(sheetColumns, 1.0f, PARTICLE_SHEET_SIDE_MAX, "The texture as a sheet of frames, read from the top left")
    VKM_F(sheetRows, 1.0f, PARTICLE_SHEET_SIDE_MAX, "The texture as a sheet of frames, read from the top left")
    VKM_F(sheetFps, 0.0f, 120.0f, "Frames a second, looping; 0 plays the sheet once a life")
    VKM_F(randomFrame, "Each particle starts on a frame of its own")
    VKM_F(sheetBlend, "Each frame fades into the next: a slow flipbook moves smoothly")
    VKM_F(collide, "Strikes the physics world's bodies, a ray each step")
    VKM_F(bounce, 0.0f, 1.0f, "The share of its speed into a surface it keeps")
    VKM_F(lifeLostOnHit, 0.0f, 1.0f, "The share of its whole life a strike takes; 1 ends it")
    VKM_F(seenBy, "Who draws its particles, as for a Mesh")
VKM_REFLECT_END()
