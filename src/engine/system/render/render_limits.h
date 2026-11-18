#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief What every render backend is sized for: the lights, clusters and shadow casters a frame holds.
 *
 * Written into every shader's prelude by the backend; never re-declare one in a shader. See
 * docs/reference/lighting.md.
 */
namespace Config {

    // Lights uploaded per frame.
    constexpr uint32_t MAX_LIGHTS = 256;

    // Forward+ grid: screen tiles by exponential depth slices. Finer clusters cost the cull, one
    // invocation per cluster testing every light; coarser ones fill each list nearer
    // MAX_LIGHTS_PER_CLUSTER, past which a light is dropped and shows as a cut.
    constexpr uint32_t CLUSTER_X = 32;
    constexpr uint32_t CLUSTER_Y = 18;
    constexpr uint32_t CLUSTER_Z = 24;
    constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 64;

    // Casters in the 2D atlas (directional + spot).
    constexpr uint32_t MAX_SHADOW_CASTERS_2D = 6;

    // Point-light casters, one depth cube each.
    constexpr uint32_t MAX_SHADOW_CASTERS_CUBE = 2;

    // Sun shadow cascades, each taking one of the MAX_SHADOW_CASTERS_2D atlas tiles.
    constexpr uint32_t NUM_CASCADES = 4;

} // namespace Config

} // namespace Vkm::Engine
