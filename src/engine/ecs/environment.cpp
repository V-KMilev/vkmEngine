#include "ecs/environment.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Vkm::Engine {

glm::vec3 Environment::directionFromAngles(float elevationDeg, float azimuthDeg) {
    const float el = glm::radians(elevationDeg);
    const float az = glm::radians(azimuthDeg);
    const float c  = std::cos(el);

    // The direction toward the body, not the way its light travels. Azimuth 0 is
    // +Z turning toward +X and elevation 90 is straight up - a definition of the
    // angles, so a change of forward convention does not reach it.
    return glm::vec3(c * std::sin(az), std::sin(el), c * std::cos(az));
}

} // namespace Vkm::Engine
