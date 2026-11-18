#pragma once

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"
#include "ecs/component/ui/ui_shape.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

/**
 * @brief A filled quad drawn over its element's resolved rect.
 *
 * A texture, if set, is stretched over the rect and multiplied by the tint.
 */
struct UIImage {
    glm::vec4     color = {1.0f, 1.0f, 1.0f, 1.0f};  ///< Straight (non-premultiplied) RGBA tint.
    UIShape       shape;                             ///< Corners, edge and fade; flat at its defaults.
    TextureHandle texture;                           ///< Picture stretched over the rect; none draws the tint
                                                     ///< alone.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::UIImage)
    VKM_F(color)
    VKM_F(shape)
    VKM_F(texture, "The picture it fills with; none fills with the color")
VKM_REFLECT_END()
