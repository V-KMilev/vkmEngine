#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Local reflection + irradiance probe.
 *
 * Captures a cubemap at the Transform's position, convolved into diffuse and
 * prefiltered specular cubes. Surfaces in the box (centred there) use these over
 * the global IBL, blending back toward it near the edge.
 */
struct ReflectionProbe {
    /// Influence box half-size (world units), for parallax correction + falloff.
    glm::vec3 halfExtents = glm::vec3(5.0f);
    /// Fraction of the half-extent fading to the global IBL.
    float     falloff     = 0.2f;
    float     intensity   = 1.0f;             ///< Linear-HDR multiplier.
    /// Asked-for face size in pixels; every probe is captured at the largest any asks.
    uint32_t  resolution  = 256;

    /**
     * @brief Bump to force a re-bake.
     *
     * For changes the params miss (sun moved, geometry edited); moving or resizing re-bakes alone.
     */
    uint32_t bakeVersion = 0;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::ReflectionProbe)
    VKM_F(halfExtents)
    VKM_F(falloff)
    VKM_F(intensity)
    VKM_F(resolution)
    VKM_F(bakeVersion)
VKM_REFLECT_END()
