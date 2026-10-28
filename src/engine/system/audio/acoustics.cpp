#include "system/audio/acoustics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <glm/gtc/constants.hpp>

#include "core/math/rotation.h"

namespace Vkm::Engine::Acoustics {

namespace {

/// How far along the lattice each slot of the window steps from the last: near ROOM_WINDOW over
/// the golden ratio, and sharing no factor with it, so the slots visit every lattice point once.
constexpr size_t SLOT_STRIDE = 41;
static_assert(std::gcd(SLOT_STRIDE, ROOM_WINDOW) == 1, "the window's slots take every direction once");

/// The share of a sound's energy a strike takes, in Eyring's formula: a lived-in interior's mean,
/// bare walls and what stands against them together.
constexpr float ABSORPTION = 0.2f;

/// Below this enclosure a space has no reverb of its own; above SHUT_IN it has all of it.
constexpr float OPEN    = 0.25f;
constexpr float SHUT_IN = 0.9f;

/// The reverb level of a space shut in on every side, at strength 1.
constexpr float FULL_WET = 0.35f;

/// How near its target an eased value lands on it.
constexpr float SETTLED = 1e-3f;

/// What the decay is held between, in seconds: a cupboard rings a little, a cathedral not forever.
constexpr float SHORTEST_DECAY = 0.2f;
constexpr float LONGEST_DECAY  = 6.0f;

} // namespace

glm::vec3 roomDirection(size_t slot) {
    const size_t index = (slot * SLOT_STRIDE) % ROOM_WINDOW;
    const float  y     = 1.0f - 2.0f * (static_cast<float>(index) + 0.5f) / static_cast<float>(ROOM_WINDOW);
    const float  r     = std::sqrt(std::max(0.0f, 1.0f - y * y));
    // The golden angle: each lattice point turned from the last by as irrational an angle as there is.
    const float turn = glm::pi<float>() * (3.0f - std::sqrt(5.0f)) * static_cast<float>(index);
    return {std::cos(turn) * r, y, std::sin(turn) * r};
}

Room estimateRoom(const RoomRay* rays, size_t count) {
    Room room;
    if (rays == nullptr || count == 0) return room;
    int   strikes = 0;
    float length  = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        strikes += rays[i].strikes;
        length  += rays[i].length;
    }
    const float legs  = static_cast<float>(count) * static_cast<float>(1 + ROOM_BOUNCES);
    room.enclosure    = static_cast<float>(strikes) / legs;
    room.meanFreePath = strikes > 0 ? length / static_cast<float>(strikes) : 0.0f;
    return room;
}

Reverb reverbFor(const Room& room, float strength) {
    Reverb reverb;
    if (!(strength > 0.0f) || !(room.meanFreePath > 0.0f)) return reverb;
    const float shut = std::clamp((room.enclosure - OPEN) / (SHUT_IN - OPEN), 0.0f, 1.0f);
    if (shut <= 0.0f) return reverb;
    // Eyring: RT60 = 0.161 V / (-S ln(1 - a)), and V / S is a quarter of the mean free path.
    const float eyring = 0.04f * room.meanFreePath / -std::log(1.0f - ABSORPTION);
    // A leaky room loses what escapes as well as what the walls take.
    reverb.decaySeconds = std::clamp(eyring * (0.4f + 0.6f * shut), SHORTEST_DECAY, LONGEST_DECAY);
    reverb.wet          = FULL_WET * shut * strength;
    return reverb;
}

std::array<glm::vec3, OCCLUSION_RAYS> occlusionTargets(glm::vec3 ear, glm::vec3 source, float radius) {
    std::array<glm::vec3, OCCLUSION_RAYS> targets;
    targets.fill(source);
    const glm::vec3 way = source - ear;
    const float     len = glm::length(way);
    if (!(len > 1e-4f)) return targets;
    // Across the line of sight: sideways and up.
    glm::vec3 side, up;
    Math::orthoBasis(way / len, side, up);
    targets[1] = source + side * radius;
    targets[2] = source - side * radius;
    targets[3] = source + up * radius;
    targets[4] = source - up * radius;
    return targets;
}

float occludedGain(float occlusion) {
    const float o = std::clamp(occlusion, 0.0f, 1.0f);
    return 1.0f + (OCCLUDED_GAIN - 1.0f) * o;
}

float occludedCutoff(float occlusion) {
    const float o = std::clamp(occlusion, 0.0f, 1.0f);
    return OPEN_CUTOFF * std::pow(OCCLUDED_CUTOFF / OPEN_CUTOFF, o);
}

float approach(float current, float target, float dt, float seconds) {
    if (!(dt > 0.0f)) return current;
    if (!(seconds > 0.0f)) return target;
    const float eased = target + (current - target) * std::exp(-dt / seconds);
    return std::abs(eased - target) < SETTLED ? target : eased;
}

} // namespace Vkm::Engine::Acoustics
