#include "ecs/environment.h"

#include <cmath>

#include "core/math/grid.h"

namespace Vkm::Engine {

glm::uvec3 FogSettings::froxelGrid() const {
    const glm::uvec3 authored(resolutionX, resolutionY, resolutionZ);
    return Math::fitGrid(authored, MIN_FROXELS, MAX_FROXELS, MAX_FROXEL_COUNT);
}

glm::vec3 Environment::directionFromAngles(float elevationDeg, float azimuthDeg) {
    const float el = glm::radians(elevationDeg);
    const float az = glm::radians(azimuthDeg);
    const float c  = std::cos(el);

    return glm::vec3(c * std::sin(az), std::sin(el), c * std::cos(az));
}

} // namespace Vkm::Engine
