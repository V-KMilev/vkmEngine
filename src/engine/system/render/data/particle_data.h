#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief One billboard particle for the frame, flattened from the emitters.
 *
 * Two vec4s so it uploads straight into a std430 SSBO the billboard vertex stage
 * indexes by instance - no vertex attributes to plumb. Size and colour are already
 * evaluated from the particle's age, so the backend just draws them.
 */
struct ParticleData {
    glm::vec4 positionSize;  ///< xyz = world position, w = world-space size.
    glm::vec4 color;         ///< Linear RGBA (alpha already faded by age).
    glm::vec4 params;        ///< x = edge softness (0 hard .. 1 soft); yzw reserved.
};

} // namespace Vkm::Engine
