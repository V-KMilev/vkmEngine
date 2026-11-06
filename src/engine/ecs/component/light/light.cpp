#include "ecs/component/light/light.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <glm/gtc/constants.hpp>

#include "ecs/scene.h"
#include "ecs/component/scene/transform.h"

namespace Vkm::Engine {

namespace {

constexpr double FLICKER_RATE = 6.0;  ///< Wavers a second at the lowest of the three rates: a flame's.

constexpr double FLICKER_NOTES[] = {1.0, 2.31, 4.17};  ///< The three rates, over the lowest: no two share a
                                                       ///< beat.

constexpr double SLOT_LEAD = 0.618034;  ///< Seconds each slot runs ahead of the one before: far from a whole
                                        ///< number of any note's period.

// A point or spot light's emitter radius, in meters: a household bulb's.
constexpr float BULB_RADIUS = 0.03f;

} // namespace

float flickerWave(const Light& light, double seconds, uint32_t slot) {
    const float depth = std::clamp(light.flicker, 0.0f, 1.0f);
    if (depth <= 0.0f || light.type == LightType::Directional) return 1.0f;
    const double now = seconds + SLOT_LEAD * static_cast<double>(slot);
    double wave = 0.0;
    for (const double note : FLICKER_NOTES) {
        // Whole cycles dropped in double, so a long-running world keeps the sine's precision.
        const double cycles = now * FLICKER_RATE * note;
        wave += std::sin(glm::two_pi<double>() * (cycles - std::floor(cycles)));
    }
    const double dim = 0.5 + 0.5 * wave / static_cast<double>(std::size(FLICKER_NOTES));
    return 1.0f - depth * static_cast<float>(dim);
}

EntityId findKeyLight(const Scene& scene) {
    const auto isKey = [](const Light& light, const Transform&) {
        return light.enabled && light.type == LightType::Directional;
    };
    return findLowestSlot<Light, Transform>(scene, isKey);
}

Light generateLight(LightType type) {
    Light light;
    light.type = type;

    // Only a spot uses cone angles; clear them so others carry no stale defaults.
    if (type != LightType::Spot) {
        light.innerConeAngle = 0.0f;
        light.outerConeAngle = 0.0f;
    }

    if (type == LightType::Point || type == LightType::Spot) light.sourceRadius = BULB_RADIUS;

    switch (type) {
        case LightType::Directional: light.radius = 0.0f;  break;  // a sun does not attenuate
        case LightType::Point:                             break;  // keeps the component's default reach
        case LightType::Spot:        light.radius = 20.0f; break;
        case LightType::Count:                             break;
    }

    return light;
}

} // namespace Vkm::Engine
