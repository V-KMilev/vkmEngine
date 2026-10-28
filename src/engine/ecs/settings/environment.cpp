#include "ecs/environment.h"

#include <algorithm>
#include <cmath>

namespace Vkm::Engine {

glm::uvec3 FogSettings::froxelGrid() const {
    const glm::uvec3 authored(resolutionX, resolutionY, resolutionZ);
    glm::uvec3 dims = glm::clamp(authored, glm::uvec3(MIN_FROXELS), glm::uvec3(MAX_FROXELS));
    const double count = static_cast<double>(dims.x) * dims.y * dims.z;
    if (count <= MAX_FROXEL_COUNT) return dims;

    // No axis grows, so flooring keeps the product under the cap.
    const double scale = std::cbrt(MAX_FROXEL_COUNT / count);
    for (int axis = 0; axis < 3; ++axis) {
        const auto scaled = static_cast<uint32_t>(std::floor(dims[axis] * scale));
        dims[axis] = std::max(scaled, MIN_FROXELS);
    }
    return dims;
}

glm::vec3 Environment::directionFromAngles(float elevationDeg, float azimuthDeg) {
    const float el = glm::radians(elevationDeg);
    const float az = glm::radians(azimuthDeg);
    const float c  = std::cos(el);

    return glm::vec3(c * std::sin(az), std::sin(el), c * std::cos(az));
}

} // namespace Vkm::Engine
