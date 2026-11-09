#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief An object the eye sees with an Outline: what the backend draws its rim with.
 */
struct OutlineData {
    uint32_t  object;        ///< Index into RenderObjects.
    glm::vec3 color;         ///< Outline::color.
    float     width;         ///< Outline::width, pixels of the window.
    bool      throughWalls;  ///< Outline::throughWalls.
};

} // namespace Vkm::Engine
