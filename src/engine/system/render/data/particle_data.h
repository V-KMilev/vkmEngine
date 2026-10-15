#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief One billboard particle for the frame, flattened from the emitters.
 *
 * Five vec4s so it uploads straight into a std430 SSBO indexed by instance.
 * Size, colour, angle and sheet frame are already evaluated from age.
 */
struct ParticleData {
    glm::vec4 positionSize;  ///< xyz = world position, w = half the billboard's width (the authored size).
    glm::vec4 color;         ///< Linear RGBA.

    /// x = edge softness (0 hard .. 1 soft); y = angle, radians; z = its texture, as 1 + its
    /// place in RenderView::particleTextures, 0 for the soft disc; w = 1 on the viewmodel layer,
    /// for a Viewmodel emitter (ParticleEmitter::seenBy) seen through a scene camera.
    glm::vec4 params;
    glm::vec4 uvRect;        ///< Its sheet frame: xy = the corner at the quad's (-1,-1), zw = the size.
    glm::vec4 trail;         ///< xyz = where it streaks to behind it, world space; zero for none. w reserved.
};

} // namespace Vkm::Engine
