#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief One billboard particle for the frame, flattened from the emitters.
 *
 * Three vec4s so it uploads straight into a std430 SSBO indexed by instance.
 * Size and colour are already evaluated from age.
 */
struct ParticleData {
    glm::vec4 positionSize;  ///< xyz = world position, w = half the billboard's width (the authored size).
    glm::vec4 color;         ///< Linear RGBA.
    glm::vec4 params;        ///< x = edge softness (0 hard .. 1 soft); yzw reserved.
};

} // namespace Vkm::Engine
