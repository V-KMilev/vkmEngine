#include "resource/generate/light_generators.h"

namespace Vkm::Engine {

namespace {

// A point or spot light's emitter radius, in metres: a household bulb's.
constexpr float BULB_RADIUS = 0.03f;

} // namespace

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
        case LightType::Rect:
        case LightType::Disk:        light.radius = 15.0f; break;
        case LightType::Count:                             break;
    }

    return light;
}

} // namespace Vkm::Engine
