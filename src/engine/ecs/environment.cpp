#include "ecs/environment.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Vkm::Engine {

glm::vec3 Environment::directionFromAngles(float elevationDeg, float azimuthDeg) {
    const float el = glm::radians(elevationDeg);
    const float az = glm::radians(azimuthDeg);
    const float c  = std::cos(el);

    // Same parameterisation the camera controller uses for yaw/pitch, so an
    // elevation reads the same here as a pitch does there: +Z at azimuth 0,
    // turning toward +X, and elevation straight up at 90.
    return glm::vec3(c * std::sin(az), std::sin(el), c * std::cos(az));
}

} // namespace Vkm::Engine
