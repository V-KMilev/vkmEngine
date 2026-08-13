#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief A filled quad drawn over its element's resolved rect.
 *
 * The simplest UI visual: a solid rectangle tinted by `color`, covering the
 * owning UIElement. The tint's alpha drives the standard src-alpha blend, so a
 * panel can be made translucent. A sprite texture is layered on in a later
 * milestone; for now this paints a flat colour.
 */
struct UIImage {
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};  ///< Straight (non-premultiplied) RGBA tint.
};

VKM_REFLECT_BEGIN(UIImage)
    VKM_F(color)
VKM_REFLECT_END()

} // namespace Engine
