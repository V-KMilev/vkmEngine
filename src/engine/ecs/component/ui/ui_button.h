#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"
#include "ecs/component/ui/ui_shape.h"

namespace Vkm::Engine {

/**
 * @brief An interactive, hit-tested button drawn over its element's rect.
 *
 * A press released over it enqueues a UIClickEvent, which BehaviorBase::onClick hears. An image,
 * the button and a text on one entity draw from one rect in that order, so a centered UIText on
 * the entity is its label, with no child needed. Also the UI's one drag primitive: UISlider is built on it,
 * following the pointer while it is `held`.
 */
struct UIButton {
    enum class State : uint8_t { Normal, Hover, Pressed, Disabled };

    glm::vec4 normalColor   = {0.18f, 0.20f, 0.26f, 0.95f};
    glm::vec4 hoverColor    = {0.26f, 0.30f, 0.40f, 0.95f};
    glm::vec4 pressedColor  = {0.12f, 0.14f, 0.18f, 0.95f};
    glm::vec4 disabledColor = {0.15f, 0.15f, 0.17f, 0.60f};

    UIShape shape;                ///< Corners, edge and fade of the quad, whatever state colors it.
    bool    interactable = true;  ///< When false: drawn Disabled, ignores the pointer.

    // Written by the UISystem every frame the button is drawn; never authored.
    State     state        = State::Normal;
    bool      held         = false;            ///< A press began on it and is still down, wherever the
                                               ///< pointer is now.
    glm::vec2 pointer      = glm::vec2(0.0f);  ///< In the element's reference pixels, from its top-left.
    glm::vec2 resolvedSize = glm::vec2(0.0f);  ///< The element's laid-out size, in the same pixels as
                                               ///< pointer.

    /**
     * @brief The background tint for the current state.
     *
     * @return The color for `state`.
     */
    const glm::vec4& colorForState() const {
        // No default, so a new state is a build warning rather than a silent Normal.
        switch (state) {
            case State::Normal:   return normalColor;
            case State::Hover:    return hoverColor;
            case State::Pressed:  return pressedColor;
            case State::Disabled: return disabledColor;
        }
        return normalColor;
    }
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::UIButton)
    VKM_F(normalColor)
    VKM_F(hoverColor)
    VKM_F(pressedColor)
    VKM_F(disabledColor)
    VKM_F(shape)
    VKM_F(interactable, "Off it takes no clicks and draws in Disabled Color")
VKM_REFLECT_END()
