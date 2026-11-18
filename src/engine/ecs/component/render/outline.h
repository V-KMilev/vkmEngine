#pragma once

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief A rim of one color around the Mesh on this entity, so it reads against anything behind it.
 *
 * Drawn after the opaque pass around what the eye sees of the mesh; through walls, around all of
 * it, wherever something stands in front.
 */
struct Outline {
    glm::vec3 color{1.0f, 0.55f, 0.1f};  ///< Linear color, as the frame shows a value of 1 after its
                                         ///< exposure: tonemapped, not exposed.
    float     width        = 2.0f;
    bool      throughWalls = false;
    bool      enabled      = true;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Outline)
    VKM_F(color, "Shown as the tonemap shows it, however bright the scene")
    VKM_F(width, 0.0f, 8.0f, "Pixels of the window")
    VKM_F(throughWalls, "Drawn where something stands in front of the mesh too")
    VKM_F(enabled, "Off costs nothing")
VKM_REFLECT_END()
