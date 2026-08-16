#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief A filled quad drawn over its element's resolved rect.
 *
 * The tint's alpha drives the standard src-alpha blend, so a panel can be made
 * translucent. The fill is a flat colour - there is no sprite texture.
 */
struct UIImage {
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};  ///< Straight (non-premultiplied) RGBA tint.
};
} // namespace Engine

VKM_REFLECT_BEGIN(::Engine::UIImage)
    VKM_F(color)
VKM_REFLECT_END()
