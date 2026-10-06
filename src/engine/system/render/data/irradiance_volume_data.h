#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief Flattened irradiance volume for the frame (see GLIrradianceVolume).
 */
struct IrradianceVolumeData {
    glm::vec3 center;       ///< World space.
    glm::vec3 halfExtents;

    uint32_t resolutionX;   ///< Probe counts per axis.
    uint32_t resolutionY;
    uint32_t resolutionZ;

    float    intensity;     ///< Linear-HDR multiplier.
    float    blendDistance; ///< Fade-in depth inside the box, metres.
    uint32_t bakeVersion;   ///< Re-bake trigger.
};

} // namespace Vkm::Engine
