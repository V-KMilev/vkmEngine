#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Makes an element's children scroll inside it, and clips them to it.
 *
 * Children lay out against the element's rect shifted by `offset`; it clips whether or
 * not UIElement::clipChildren is set. The wheel goes to the nearest scroll view holding
 * whatever is drawn on top under the pointer, and lands on the next frame's layout,
 * since deciding needs the whole frame's rects. A scrollbar is
 * `offset / (contentSize - viewSize)`.
 */
struct UIScroll {
    /**
     * @brief How far the content has scrolled, in canvas reference pixels.
     *
     * Clamped every frame to the overflow, so it can be written freely.
     */
    glm::vec2 offset = {0.0f, 0.0f};

    /// Reference pixels the content moves per wheel notch.
    float wheelStep = 60.0f;

    /**
     * @brief Extent of the content laid out inside, in reference pixels; resolved, not authored.
     *
     * Measured from what descendants drew, so an empty layout box adds nothing.
     */
    glm::vec2 contentSize = {0.0f, 0.0f};

    /// This element's resolved size, in reference pixels.
    glm::vec2 viewSize = {0.0f, 0.0f};

    /**
     * @brief How far the content can travel on each axis, never negative; `offset`'s clamp.
     *
     * @return Overflow per axis in reference pixels.
     */
    glm::vec2 range() const { return glm::max(contentSize - viewSize, glm::vec2(0.0f)); }
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::UIScroll)
    VKM_F(offset)
    VKM_F(wheelStep)
VKM_REFLECT_END()
