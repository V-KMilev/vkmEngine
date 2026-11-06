#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Local reflection + irradiance probe.
 *
 * Captures a cubemap at the Transform's position, convolved into diffuse and
 * prefiltered specular cubes. Surfaces in the box (centered there) use these over
 * the global IBL, blending back toward it near the edge.
 */
struct ReflectionProbe {
    glm::vec3 halfExtents = glm::vec3(5.0f);  ///< Influence box half-size (world units), for parallax
                                              ///< correction + falloff.
    float     falloff     = 0.2f;             ///< Fraction of the half-extent fading to the global IBL.
    float     intensity   = 1.0f;             ///< Linear-HDR multiplier.
    uint32_t  resolution  = 256;              ///< Asked-for face size in pixels; every probe is captured at
                                              ///< the largest any asks.

    uint32_t bakeVersion = 0;  ///< Bump to force a re-bake. For changes the params miss (geometry edited);
                               ///< moving or resizing re-bakes alone.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::ReflectionProbe)
    VKM_F(halfExtents, 0.1f, 1000.0f, "Half its box, which should match the room's walls")
    VKM_F(falloff, 0.0f, 1.0f, "The share of the box over which it fades to the sky")
    VKM_F(intensity, 0.0f, 8.0f, "Scales what it reflects")
    VKM_F(resolution, "Cube face size for the bake; the largest any probe asks wins")
    VKM_F(bakeVersion)
VKM_REFLECT_END()
