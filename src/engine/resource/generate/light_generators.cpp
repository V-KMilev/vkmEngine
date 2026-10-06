#include "resource/generate/light_generators.h"

namespace Vkm::Engine {

Light generateLight(LightType type) {
    Light light;
    light.type = type;

    // Only a spot uses cone angles; clear them so others carry no stale defaults.
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
