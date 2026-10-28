#pragma once

#include <array>
#include <cstddef>

#include <glm/glm.hpp>

namespace Vkm::Engine::Acoustics {

/**
 * @brief Rays from the ear to a source, through what stands between: one at its middle and four
 *        around it - either side, above and below - so a doorway lets part of a sound through.
 */
inline constexpr size_t OCCLUSION_RAYS = 5;

/// Bounces a room ray follows after its first strike.
inline constexpr int ROOM_BOUNCES = 2;

/// How far a room ray looks before it calls itself out in the open, in metres.
inline constexpr float ROOM_REACH = 60.0f;

/// The room rays the estimate is taken over, a few a frame, so the room is re-measured every
/// ROOM_WINDOW / ROOM_RAYS_PER_FRAME frames.
inline constexpr size_t ROOM_WINDOW         = 64;
inline constexpr size_t ROOM_RAYS_PER_FRAME = 4;

/// How far short of its target each occlusion ray stops, in metres, so a surface the source is
/// pressed against does not block it.
inline constexpr float SOURCE_CLEARANCE = 0.1f;

/// The gain a sound keeps when every occlusion ray is blocked.
inline constexpr float OCCLUDED_GAIN = 0.4f;

/// The low-pass the occlusion's scale starts from, the top of hearing, in hertz.
inline constexpr float OPEN_CUTOFF = 20000.0f;

/// The low-pass a fully blocked sound is heard through, in hertz.
inline constexpr float OCCLUDED_CUTOFF = 700.0f;

/// Seconds a voice's occlusion takes to move most of the way to a new value; quick enough to
/// follow a door, slow enough not to flutter as the ear grazes a corner.
inline constexpr float OCCLUSION_SECONDS = 0.12f;

/// Seconds the room's reverb takes to follow the ear into another space.
inline constexpr float ROOM_SECONDS = 0.6f;

/**
 * @brief What one surface strike tells a ray: where it struck and which way the surface faces.
 */
struct Strike {
    float     distance = 0.0f;           ///< Along the ray, in metres.
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};  ///< Out of the surface, back along the ray.
};

/**
 * @brief One room ray's journey: how many of its legs struck something, how long they were, and
 *        the way it went.
 */
struct RoomRay {
    int   strikes = 0;     ///< Legs that struck a surface, of 1 + ROOM_BOUNCES.
    float length  = 0.0f;  ///< The struck legs' summed length, in metres.
    /// Where it left the ear, then each strike: the first 1 + strikes.
    std::array<glm::vec3, 2 + ROOM_BOUNCES> path{};
    /// The way its last leg went out into the open, striking nothing; zero when every leg struck.
    glm::vec3 escape{0.0f};
};

/**
 * @brief What the rays say about the space the ear is in.
 */
struct Room {
    /// 0 out in the open, 1 shut in on every side: the share of the rays' legs that struck.
    float enclosure = 0.0f;
    /// The struck legs' mean length, in metres: how big the space is. 0 when nothing struck.
    float meanFreePath = 0.0f;
};

/**
 * @brief The reverb a room asks for.
 */
struct Reverb {
    float decaySeconds = 0.0f;  ///< To fall by 60 dB.
    float wet          = 0.0f;  ///< Linear level of the reverberation beside the dry sound.
};

/**
 * @brief The direction of the room ray in @p slot of the window.
 *
 * The window's directions cover the sphere evenly, a Fibonacci lattice with none favoured, and
 * neighbouring slots lie far apart on it, so a window only part refilled is not lopsided.
 *
 * @param slot Below ROOM_WINDOW.
 * @return A unit direction.
 */
glm::vec3 roomDirection(size_t slot);

/**
 * @brief Follow one room ray from @p origin, reflecting it off what it strikes.
 *
 * @tparam Cast     bool(origin, direction, maxDistance, Strike&): the nearest surface along a ray.
 * @param cast      Asks the world.
 * @param origin    Where the ear is.
 * @param direction Which way the ray leaves; unit length.
 * @return Its strikes and their length.
 */
template <typename Cast>
RoomRay traceRoomRay(const Cast& cast, glm::vec3 origin, glm::vec3 direction) {
    RoomRay ray;
    ray.path[0] = origin;
    for (int leg = 0; leg <= ROOM_BOUNCES; ++leg) {
        Strike strike;
        if (!cast(origin, direction, ROOM_REACH, strike)) {
            ray.escape = direction;
            break;
        }
        ++ray.strikes;
        ray.length += strike.distance;
        ray.path[static_cast<size_t>(ray.strikes)] = strike.point;
        // Off the surface a little, or the next leg starts inside it.
        direction = glm::reflect(direction, strike.normal);
        origin    = strike.point + strike.normal * 1e-3f;
    }
    return ray;
}

/**
 * @brief How shut in a space is and how big, from the rays traced in it.
 *
 * @param rays  The window's rays: traceRoomRay's from the ear along each slot's roomDirection.
 * @param count How many of its slots hold one, from the first.
 * @return The room; out in the open when there are none.
 */
Room estimateRoom(const RoomRay* rays, size_t count);

/**
 * @brief The reverb @p room asks for, scaled by @p strength.
 *
 * Its decay follows Eyring's reverberation time, which for a room of mean free path l is
 * 0.04 l / -ln(1 - a), with the absorption a of a lived-in interior; a leaky room decays sooner
 * and is quieter, and open ground has none.
 *
 * @param room     What the rays measured.
 * @param strength The scene's scale on the level; 0 turns reverb off.
 * @return Decay and level.
 */
Reverb reverbFor(const Room& room, float strength);

/**
 * @brief The points around a source the occlusion rays aim at: its middle, then either side of it
 *        and above and below, across the line from the ear.
 *
 * @param ear    Where the listener is.
 * @param source Where the sound is.
 * @param radius How far either side of the source the side rays aim.
 * @return OCCLUSION_RAYS points, the middle first.
 */
std::array<glm::vec3, OCCLUSION_RAYS> occlusionTargets(glm::vec3 ear, glm::vec3 source, float radius);

/**
 * @brief Cast the rays from the ear to a source, one to each of occlusionTargets.
 *
 * A side target the sound cannot reach - inside the floor it sits on, or a wall beside it - is
 * first pulled back halfway to what stops it, so a sound in plain view is not muffled by the
 * surface it is on. Each ray then stops SOURCE_CLEARANCE short of its target, so a sound pressed
 * against a surface is not blocked by that; a target nearer than that is not cast at all. The body
 * a sound sits in is @p cast's to pass through.
 *
 * @tparam Cast  As traceRoomRay's.
 * @tparam Visit void(const glm::vec3& target, const Strike* blocked): what stopped the ray, or null.
 * @param cast   Asks the world.
 * @param ear    Where the listener is.
 * @param source Where the sound is.
 * @param radius How far either side the side rays aim.
 * @param visit  Told of each ray cast.
 */
template <typename Cast, typename Visit>
void castOcclusionRays(const Cast& cast, glm::vec3 ear, glm::vec3 source, float radius, const Visit& visit) {
    std::array<glm::vec3, OCCLUSION_RAYS> targets = occlusionTargets(ear, source, radius);
    for (size_t side = 1; side < OCCLUSION_RAYS; ++side) {
        const glm::vec3 out  = targets[side] - source;
        const float     span = glm::length(out);
        Strike          wall;
        if (span > 0.0f && cast(source, out / span, span, wall)) {
            targets[side] = source + out * (0.5f * wall.distance / span);
        }
    }
    for (const glm::vec3& target : targets) {
        const glm::vec3 way   = target - ear;
        const float     reach = glm::length(way) - SOURCE_CLEARANCE;
        if (!(reach > 0.0f)) continue;
        Strike strike;
        const bool stopped = cast(ear, way / glm::length(way), reach, strike);
        visit(target, stopped ? &strike : nullptr);
    }
}

/**
 * @brief The share of the rays from the ear to a source (castOcclusionRays) that something stops.
 *
 * @tparam Cast  As traceRoomRay's.
 * @param cast   Asks the world.
 * @param ear    Where the listener is.
 * @param source Where the sound is.
 * @param radius How far either side the side rays aim.
 * @return 0 heard straight, 1 behind a wall.
 */
template <typename Cast>
float blockedShare(const Cast& cast, glm::vec3 ear, glm::vec3 source, float radius) {
    size_t blocked = 0;
    castOcclusionRays(cast, ear, source, radius, [&blocked](const glm::vec3&, const Strike* stopped) {
        if (stopped) ++blocked;
    });
    return static_cast<float>(blocked) / static_cast<float>(OCCLUSION_RAYS);
}

/**
 * @brief The gain an occlusion keeps of a sound.
 *
 * @param occlusion 0 heard straight, 1 behind a wall.
 * @return From 1 down to OCCLUDED_GAIN.
 */
float occludedGain(float occlusion);

/**
 * @brief The low-pass cutoff an occlusion is heard through.
 *
 * Falls on a logarithmic scale, as pitch is heard, from OPEN_CUTOFF to OCCLUDED_CUTOFF. A sound
 * with no occlusion at all wants no filter, and AudioDevice opens its low-pass entirely.
 *
 * @param occlusion 0 heard straight, 1 behind a wall.
 * @return The cutoff, in hertz.
 */
float occludedCutoff(float occlusion);

/**
 * @brief Move @p current toward @p target, most of the way in @p seconds.
 *
 * Framerate-independent: two half frames move it as one whole frame does. Close enough, it lands
 * on @p target exactly, so a sound eased out from behind a wall ends with its filter open.
 *
 * @param current Where it is.
 * @param target  Where it is going.
 * @param dt      Seconds since the last step.
 * @param seconds Its time constant.
 * @return The new value.
 */
float approach(float current, float target, float dt, float seconds);

} // namespace Vkm::Engine::Acoustics
