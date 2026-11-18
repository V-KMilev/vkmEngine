#pragma once

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief How a filled quad is drawn beyond its flat tint: corners, an edge, a fade.
 *
 * Defaults draw the flat rectangle. A radius past half the shorter side is clamped
 * there. The gradient runs from the quad's own color (a button's state color) down.
 */
struct UIShape {
    float     cornerRadius = 0.0f;                      ///< Reference pixels.
    float     borderWidth  = 0.0f;                      ///< Edge stroke inside the rect, reference pixels; 0
                                                        ///< = none.
    glm::vec4 borderColor  = {1.0f, 1.0f, 1.0f, 1.0f};  ///< The stroke's straight RGBA.
    bool      gradient     = false;                     ///< Fade the fill toward @ref bottomColor at the
                                                        ///< bottom edge.
    glm::vec4 bottomColor  = {1.0f, 1.0f, 1.0f, 1.0f};  ///< The fill at the bottom edge when @ref gradient is
                                                        ///< set.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::UIShape)
    VKM_F(cornerRadius, 0.0f, 4096.0f, "Corner rounding in reference pixels")
    VKM_F(borderWidth, 0.0f, 512.0f, "An edge drawn inside the rect; 0 is none")
    VKM_F(borderColor)
    VKM_F(gradient, "Fades the fill to Bottom Color at the bottom edge")
    VKM_F(bottomColor)
VKM_REFLECT_END()
