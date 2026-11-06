#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief Flattened reflection probe for the frame, blended over the global IBL in its box. The renderer
 *        bakes and re-bakes it.
 */
struct ProbeData {
    glm::vec3 position;     ///< World space.
    glm::vec3 halfExtents;  ///< Influence box, for parallax correction + falloff.
    float     falloff;      ///< Fraction of the half-extent fading to the global IBL.
    float     intensity;    ///< Linear-HDR multiplier.
    uint32_t  resolution;   ///< Requested capture face size.
    uint32_t  bakeVersion;  ///< Re-bake trigger.

    uint32_t  entitySlot = 0;  ///< The entity slot this probe was gathered from; keys its baked capture. Not
                               ///< list position: SparseSet packing moves probes when one is destroyed.
};

} // namespace Vkm::Engine
