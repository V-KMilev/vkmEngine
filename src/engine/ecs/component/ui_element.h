#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief An axis-aligned screen-space rectangle (top-left origin, pixels).
 *
 * The unit the UI layout pass works in: parent rects seed child resolution,
 * every element resolves to one, and hit-testing is a containment check.
 */
struct UIRect {
    glm::vec2 pos  = {0.0f, 0.0f};  ///< Top-left corner.
    glm::vec2 size = {0.0f, 0.0f};

    glm::vec2 max() const { return pos + size; }

    bool contains(const glm::vec2& point) const {
        return point.x >= pos.x && point.x < pos.x + size.x
            && point.y >= pos.y && point.y < pos.y + size.y;
    }
};

/**
 * @brief The 2D rect of a UI node - the screen-space analogue of Transform.
 *
 * An element hangs off a point of its parent rect (the canvas for a top-level
 * element, otherwise the parent UIElement). `anchor` and `pivot` are normalised
 * 0..1 with a top-left origin: `anchor` picks the point of the parent to pin to,
 * `pivot` picks the point of the element that lands there. That pair keeps an
 * element fixed to a corner / edge / centre regardless of resolution. `position`
 * and `size` are authored in the canvas's reference pixels; the UISystem
 * multiplies them by the canvas scale and writes the resolved rect into
 * `screenRect` each frame for the renderer and hit-testing.
 */
struct UIElement {
    glm::vec2 anchor   = {0.5f, 0.5f};      ///< Parent anchor point, normalised (0,0 = top-left, 1,1 = bottom-right).
    glm::vec2 pivot    = {0.5f, 0.5f};      ///< Element pivot, normalised; the point placed at the anchor.
    glm::vec2 position = {0.0f, 0.0f};      ///< Offset from the anchor, in canvas reference pixels.
    glm::vec2 size     = {100.0f, 100.0f};  ///< Element size, in canvas reference pixels.
    bool      visible  = true;              ///< Skip this element and its whole subtree when false.

    UIRect screenRect = {};                 ///< Resolved rect in screen pixels (written by UISystem).
};

// screenRect is resolved every frame, so only the authored fields are
// reflected (and thus serialized).
} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::UIElement)
    VKM_F(anchor),
    VKM_F(pivot),
    VKM_F(position),
    VKM_F(size),
    VKM_F(visible)
VKM_REFLECT_END()
