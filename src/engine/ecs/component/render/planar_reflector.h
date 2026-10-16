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
    bool      enabled     = true;
    glm::vec2 halfExtents = glm::vec2(2.0f);  ///< Along the entity's X and Z, metres; not scaled.
    float     resolution  = 0.5f;             ///< Its picture's size, a share of the frame's.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::PlanarReflector)
    VKM_F(enabled)
    VKM_F(halfExtents)
    VKM_F(resolution)
VKM_REFLECT_END()
