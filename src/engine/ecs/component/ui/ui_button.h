#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "core/reflect.h"
#include "ecs/component/ui/ui_shape.h"

namespace Vkm::Engine {

/**
 * @brief An interactive, hit-tested button drawn over its element's rect.
 *
 * A press released over it enqueues a UIClickEvent carrying `eventId`; add a UIText on
 * the entity for a label. Also the UI's one drag primitive: a slider is a button over
 * its track reading `pointer.x / resolvedSize.x` while `held`.
 */
struct UIButton {
    enum class State : uint8_t { Normal, Hover, Pressed, Disabled };

    glm::vec4 normalColor   = {0.18f, 0.20f, 0.26f, 0.95f};
    glm::vec4 hoverColor    = {0.26f, 0.30f, 0.40f, 0.95f};
    glm::vec4 pressedColor  = {0.12f, 0.14f, 0.18f, 0.95f};
    glm::vec4 disabledColor = {0.15f, 0.15f, 0.17f, 0.60f};

    UIShape     shape;                  ///< Corners, edge and fade of the quad, whatever state colours it.
    std::string eventId;                ///< Identifier carried by the UIClickEvent this button fires.
    bool        interactable = true;    ///< When false: drawn Disabled, ignores the pointer.

    // Written by the UISystem every frame the button is drawn; never authored.
    State     state        = State::Normal;
    /// A press began on it and is still down, wherever the pointer is now.
    bool      held         = false;
    /// In the element's reference pixels, from its top-left.
    glm::vec2 pointer      = glm::vec2(0.0f);
    /// The element's laid-out size, in the same pixels as pointer.
    glm::vec2 resolvedSize = glm::vec2(0.0f);

    /**
     * @brief The background tint for the current state.
     *
     * @return The colour for `state`.
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
    VKM_F(eventId)
    VKM_F(interactable)
VKM_REFLECT_END()
