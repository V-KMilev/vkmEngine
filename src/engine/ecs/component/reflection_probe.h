#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief Local reflection + irradiance probe.
 *
 * Captures the surrounding scene into a cubemap from the entity's Transform
 * position, then convolves it into a diffuse irradiance cube and a roughness-
 * prefiltered specular cube. Surfaces inside the probe's influence box sample
 * those instead of the single global IBL, blended back toward the global set
 * near the box edge.
 *
 * Pure data - the bake and the per-fragment blend live in the render backend.
 * World position comes from the entity's Transform (move the entity, move the
 * probe); the box is centred on that position.
 */
struct ReflectionProbe {
    glm::vec3 halfExtents = glm::vec3(5.0f);  ///< Influence box half-size (world units), for parallax correction + falloff.
    float     falloff     = 0.2f;             ///< Fraction of the box half-extent over which influence fades to the global IBL.
    float     intensity   = 1.0f;             ///< Linear-HDR multiplier on the probe's contribution.
    uint32_t  resolution  = 256;              ///< Captured cube face size in pixels.

    /**
     * @brief Bump to force a re-bake (e.g. after moving the sun or scene geometry the
     * probe's own params don't capture). Moving the probe re-bakes automatically.
     */
    uint32_t bakeVersion = 0;
};
} // namespace Engine

VKM_REFLECT_BEGIN(::Engine::ReflectionProbe)
    VKM_F(halfExtents),
    VKM_F(falloff),
    VKM_F(intensity),
    VKM_F(resolution),
    VKM_F(bakeVersion)
VKM_REFLECT_END()
