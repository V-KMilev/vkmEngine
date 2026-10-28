#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief A flat mirror: a rectangle on the entity's XZ plane, facing its +Y, that reflects the
 *        scene as it is - what is behind the camera, off the screen or facing away included.
 *
 * Glossy surfaces lying on the rectangle - a mirror's glass, a pond, a polished floor - reflect
 * it in place of the screen-space trace and the probes. Each one seen costs a second drawing of
 * the scene from the mirrored camera, so few are seen at once: the backend draws the ones
 * covering the most of the screen and leaves the rest to the trace and the probes.
 */
struct PlanarReflector {
    static constexpr float MIN_RENDER_SCALE = 0.1f;  ///< The smallest picture, a share of the frame's.
    static constexpr float MAX_RENDER_SCALE = 1.0f;  ///< The largest: the frame's own size.

    bool      enabled     = true;
    /// Along the entity's X and Z, metres; not scaled. 0 on either reflects nothing.
    glm::vec2 halfExtents = glm::vec2(2.0f);
    float     renderScale = 0.5f;  ///< Its picture's size, a share of the frame's.
    /// Metres from its centre within which the scene is drawn in it, and no sky: black past that, as
    /// past a closed room's walls, which is what it is for. 0 draws all the camera would, and the sky.
    float     drawDistance = 0.0f;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::PlanarReflector)
    VKM_F(enabled)
    VKM_F(halfExtents)
    VKM_F(renderScale)
    VKM_F(drawDistance)
VKM_REFLECT_END()
