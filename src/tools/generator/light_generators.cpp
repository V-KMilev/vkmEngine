#include "generator/light_generators.h"

namespace Vkm::Engine {

Light generateLight(LightType type) {
    Light light;
    light.type = type;

    // Cone angles belong to a spot alone; clear them elsewhere so the inspector
    // doesn't show stale spot defaults on a light that ignores them.
    if (type != LightType::Spot) {
        light.innerConeAngle = 0.0f;
        light.outerConeAngle = 0.0f;
    }

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
