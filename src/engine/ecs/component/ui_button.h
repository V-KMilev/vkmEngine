#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief An interactive, hit-tested button drawn over its element's rect.
 *
 * Each frame the UISystem tests the pointer against the owning UIElement, drives
 * the visual `state`, and - when a press is released over the button - enqueues a
 * UIClickEvent carrying `eventId` for gameplay to react to. The four tints choose
 * the background colour per state, so a button needs only a UIElement + UIButton
 * (parent a UIText under it for a label). Set `interactable = false` to grey it
 * out and ignore the pointer.
 */
struct UIButton {
    enum class State : uint8_t { Normal, Hover, Pressed, Disabled };

    glm::vec4 normalColor   = {0.18f, 0.20f, 0.26f, 0.95f};
    glm::vec4 hoverColor    = {0.26f, 0.30f, 0.40f, 0.95f};
    glm::vec4 pressedColor  = {0.12f, 0.14f, 0.18f, 0.95f};
    glm::vec4 disabledColor = {0.15f, 0.15f, 0.17f, 0.60f};

    std::string eventId;                ///< Identifier carried by the UIClickEvent this button fires.
    bool        interactable = true;    ///< When false: drawn Disabled, ignores the pointer.

    State state = State::Normal;        ///< Current visual state (written by the UISystem).

    /**
     * @brief The background tint for the current state.
     */
    const glm::vec4& colorForState() const {
        switch (state) {
            case State::Hover:    return hoverColor;
            case State::Pressed:  return pressedColor;
            case State::Disabled: return disabledColor;
            case State::Normal:
            default:              return normalColor;
        }
    }
};

// `state` is runtime-only (driven by the UISystem), so it is not reflected.
VKM_REFLECT_BEGIN(UIButton)
    VKM_F(normalColor),
    VKM_F(hoverColor),
    VKM_F(pressedColor),
    VKM_F(disabledColor),
    VKM_F(eventId),
    VKM_F(interactable)
VKM_REFLECT_END()

} // namespace Engine
