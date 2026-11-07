#include "frame/light/gl_shadow_shape.h"

#include <glm/gtc/constants.hpp>

#include "system/render/data/light_data.h"

namespace Vkm::Engine {

bool spotCastsThroughCube(const LightData& light) {
    return light.type == LightType::Spot && 2.0f * light.outerConeAngle * SPOT_MAP_MARGIN > SPOT_MAP_WIDEST;
}

bool castsThroughCube(const LightData& light) {
    return light.type == LightType::Point || spotCastsThroughCube(light);
}

float spotMapFov(const LightData& light) {
    return glm::clamp(2.0f * light.outerConeAngle * SPOT_MAP_MARGIN, glm::radians(1.0f), SPOT_MAP_WIDEST);
}

} // namespace Vkm::Engine
