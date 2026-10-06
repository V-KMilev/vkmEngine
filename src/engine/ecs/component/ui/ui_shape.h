#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief How a filled quad is drawn beyond its flat tint: corners, an edge, a fade.
 *
 * Defaults draw the flat rectangle. A radius past half the shorter side is clamped
 * there. The gradient runs from the quad's own colour (a button's state colour) down.
 */
struct UIShape {
    float     cornerRadius = 0.0f;                       ///< Reference pixels.
    /// Edge stroke inside the rect, reference pixels; 0 = none.
    float     borderWidth  = 0.0f;
    glm::vec4 borderColor  = {1.0f, 1.0f, 1.0f, 1.0f};   ///< The stroke's straight RGBA.
    /// Fade the fill toward @ref bottomColor at the bottom edge.
    bool      gradient     = false;
    /// The fill at the bottom edge when @ref gradient is set.
    glm::vec4 bottomColor  = {1.0f, 1.0f, 1.0f, 1.0f};
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::UIShape)
    VKM_F(cornerRadius)
    VKM_F(borderWidth)
    VKM_F(borderColor)
    VKM_F(gradient)
    VKM_F(bottomColor)
VKM_REFLECT_END()
