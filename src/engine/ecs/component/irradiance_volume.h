#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief Baked global illumination: a grid of irradiance probes filling a box.
 *
 * Each grid point captures the surrounding scene once, offline, and stores the
 * result as spherical harmonics. Dynamic objects inside the box then read their
 * indirect diffuse by trilinearly sampling that grid - the Source 2 model:
 * cheap at runtime (a few texture fetches, no per-frame GI work), crisp, and
 * with no temporal filtering to smear.
 *
 * The box is centred on the entity's Transform. Reflections keep coming from the
 * reflection probes; this supplies only the diffuse half.
 *
 * Pure data - the bake and the per-fragment lookup live in the render backend.
 */
struct IrradianceVolume {
    glm::vec3 halfExtents = glm::vec3(10.0f, 5.0f, 10.0f);  ///< Volume box half-size (world units).

    // Probe counts per axis. The grid is (x * y * z) probes; every one is a
    // scene capture at bake time, so raising these costs bake time, not frame time.
    uint32_t resolutionX = 8;
    uint32_t resolutionY = 4;
    uint32_t resolutionZ = 8;

    float intensity = 1.0f;  ///< Linear-HDR multiplier on the volume's contribution.

    /**
     * @brief Bump to force a re-bake (the scene changed in a way the params don't
     * capture - sun moved, geometry edited). Moving or resizing the volume
     * re-bakes automatically.
     */
    uint32_t bakeVersion = 0;
};
} // namespace Engine

VKM_REFLECT_BEGIN(::Engine::IrradianceVolume)
    VKM_F(halfExtents),
    VKM_F(resolutionX),
    VKM_F(resolutionY),
    VKM_F(resolutionZ),
    VKM_F(intensity),
    VKM_F(bakeVersion)
VKM_REFLECT_END()
