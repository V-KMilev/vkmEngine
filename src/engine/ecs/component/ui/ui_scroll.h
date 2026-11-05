#pragma once

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Makes an element's children scroll inside it, and clips them to it.
 *
 * Children lay out against the element's rect shifted by `offset`; it clips whether or
 * not UIElement::clipChildren is set. The wheel scrolls the innermost scroll view under the
 * pointer that no other blocker covers, else the one holding the topmost blocker, on whichever
 * axis overflows, vertical first; over a blocker input().wheel() reads zero, so scrolling a list
 * is never also a gameplay action. It lands on the next frame's layout, since deciding needs the
 * whole frame's rects. A scrollbar is
 * `offset / (contentSize - viewSize)`.
 */
struct UIScroll {
    glm::vec2 offset = {0.0f, 0.0f};  ///< How far the content has scrolled, in canvas reference pixels.
                                      ///< Clamped every frame to the overflow, so it can be written freely.

    float wheelStep = 60.0f;  ///< Reference pixels the content moves per wheel notch.

    glm::vec2 contentSize = {0.0f, 0.0f};  ///< Extent of the content laid out inside, in reference pixels;
                                           ///< resolved, not authored. Measured from what descendants drew,
                                           ///< so an empty layout box adds nothing, and a subtree that clips
                                           ///< itself reports only its rect.

    glm::vec2 viewSize = {0.0f, 0.0f};  ///< This element's resolved size, in reference pixels.

    /**
     * @brief How far the content can travel on each axis, never negative; `offset`'s clamp.
     *
     * @return Overflow per axis in reference pixels.
     */
    glm::vec2 range() const { return glm::max(contentSize - viewSize, glm::vec2(0.0f)); }
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::UIScroll)
    VKM_F(offset, "How far the content has scrolled, in reference pixels")
    VKM_F(wheelStep, 1.0f, 500.0f, "Reference pixels a wheel notch moves it")
VKM_REFLECT_END()
