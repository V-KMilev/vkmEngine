#include "ecs/environment.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Vkm::Engine {

glm::vec3 Environment::directionFromAngles(float elevationDeg, float azimuthDeg) {
    const float el = glm::radians(elevationDeg);
    const float az = glm::radians(azimuthDeg);
    const float c  = std::cos(el);

    // The direction TOWARD the body, not the way its light travels. Azimuth 0
    // is +Z and turns toward +X, and elevation 90 is straight up - a definition
    // of the angles rather than anything the forward convention decides, so a
    // change of forward does not reach it.
    return glm::vec3(c * std::sin(az), std::sin(el), c * std::cos(az));
}

} // namespace Vkm::Engine
