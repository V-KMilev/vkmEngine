#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief A UIButton that is on or off, flipped by its click.
 *
 * The UISystem flips `on` and enqueues a UIValueEvent (1 on, 0 off) beside the
 * button's UIClickEvent, so `on` is already flipped when either is delivered. Every
 * frame it stands `knob`, if named, at its parent's left end when off and its right
 * when on (the toggle owns its UIElement::anchor.x and pivot.x) - so best the button's
 * child, to slide along the button - and while on draws
 * the button in `onColor` where it would have drawn normalColor, unless that is clear.
 */
struct UIToggle {
    bool      on      = false;                     ///< Write it to set the toggle.
    glm::vec4 onColor = {0.0f, 0.0f, 0.0f, 0.0f};  ///< The button at rest while on; clear keeps normalColor.

    EntityId knob{};  ///< Slid to the end that says the state; none for no knob.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::UIToggle)
    VKM_F(on)
    VKM_F(onColor)
VKM_REFLECT_END()
