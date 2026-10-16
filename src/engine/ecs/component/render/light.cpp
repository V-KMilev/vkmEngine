#include "ecs/component/render/light.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <glm/gtc/constants.hpp>

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

namespace {

/// Wavers a second at the lowest of the three rates: a flame's.
constexpr double FLICKER_RATE = 6.0;

/// The three rates, over the lowest: no two share a beat.
constexpr double FLICKER_NOTES[] = {1.0, 2.31, 4.17};

/// Seconds each slot runs ahead of the one before: far from a whole number of any note's period.
constexpr double SLOT_LEAD = 0.618034;

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

} // namespace Vkm::Engine
