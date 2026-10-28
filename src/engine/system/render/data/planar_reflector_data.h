#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief A PlanarReflector for the frame, in world space.
 */
struct PlanarReflectorData {
    glm::vec3 center;        ///< The rectangle's middle, on the plane.
    glm::vec3 normal;        ///< The side it reflects, unit.
    glm::vec3 axisU;         ///< Unit, along the rectangle's first half-extent.
    glm::vec3 axisV;         ///< Unit, along its second.
    glm::vec2 halfExtents;   ///< Metres along axisU and axisV.
    float     renderScale;   ///< Its picture's size, a share of the frame's.
    float     drawDistance;  ///< Metres from center within which the scene is drawn, no sky; 0 for all.
};

} // namespace Vkm::Engine
