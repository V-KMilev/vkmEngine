#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Flattened irradiance volume for the frame.
 *
 * Snapshotted from the scene's IrradianceVolume components so the backend never
 * searches the scene. The backend bakes an SH probe grid filling the box, then
 * the forward pass samples it for indirect diffuse inside that box.
 */
struct IrradianceVolumeData {
    glm::vec3 center;       ///< World-space box centre (from the WorldTransform when parented).
    glm::vec3 halfExtents;  ///< Box half-size.

    uint32_t resolutionX;   ///< Probe counts per axis.
    uint32_t resolutionY;
    uint32_t resolutionZ;

    float    intensity;     ///< Linear-HDR multiplier on the volume's contribution.
    uint32_t bakeVersion;   ///< Re-bake trigger (snapshot of the component's bakeVersion).
};

} // namespace Engine
