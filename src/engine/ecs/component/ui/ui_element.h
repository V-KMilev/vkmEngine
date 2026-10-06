#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief An axis-aligned screen-space rectangle (top-left origin, pixels).
 */
struct UIRect {
    glm::vec2 pos  = {0.0f, 0.0f};  ///< Top-left corner.
    glm::vec2 size = {0.0f, 0.0f};

    glm::vec2 max() const { return pos + size; }

    bool contains(const glm::vec2& point) const {
        return point.x >= pos.x && point.x < pos.x + size.x
            && point.y >= pos.y && point.y < pos.y + size.y;
    }

    /**
     * @brief The overlap of this rect and @p other, empty when they miss.
     *
     * Never a negative size, since clipping composes down the hierarchy by intersecting.
     *
     * @param other Rect to intersect with.
     * @return The overlap, or a zero-size rect at this rect's corner.
     */
    UIRect intersected(const UIRect& other) const {
        const glm::vec2 lo = glm::max(pos, other.pos);
        const glm::vec2 hi = glm::min(max(), other.max());
        return UIRect{lo, glm::max(hi - lo, glm::vec2(0.0f))};
    }
};

/**
 * @brief The 2D rect of a UI node - the screen-space analogue of Transform.
 *
 * The element's `pivot` point is pinned to its parent rect's `anchor` point (both
 * normalised, top-left origin). `position` and `size` are in canvas reference pixels.
 */
struct UIElement {
    /// Parent anchor point, normalised (0,0 = top-left, 1,1 = bottom-right).
    glm::vec2 anchor   = {0.5f, 0.5f};
    glm::vec2 pivot    = {0.5f, 0.5f};      ///< Element pivot, normalised; the point placed at the anchor.
    glm::vec2 position = {0.0f, 0.0f};      ///< Offset from the anchor, in canvas reference pixels.
    /// Element size, in canvas reference pixels, added to relativeSize's share.
    glm::vec2 size     = {100.0f, 100.0f};

    /**
     * @brief The share of the parent's size this element takes, added to `size`.
     *
     * (1, 1) with size (-40, -40) at a centred anchor is a 20 inset on every side. A
     * resolved size below zero is zero.
     */
    glm::vec2 relativeSize = {0.0f, 0.0f};

    bool visible = true;  ///< Skip this element and its whole subtree when false.

    /**
     * @brief Whether this element stops a pointer reaching what is behind it.
     *
     * Consulted only on a UIImage or UIButton; clear it for a click-through overlay.
     */
    bool blocksPointer = true;

    /**
     * @brief Whether this element's rect bounds what its descendants may draw.
     *
     * Off by default, since it costs a scissor change. Bounds the pointer as well as
     * the pixels. A UIScroll clips whether or not this is set.
     */
    bool clipChildren = false;

    UIRect screenRect = {};  ///< Resolved rect in screen pixels, written each frame.

    /**
     * @brief An element pinned to one normalised point of its parent by the matching point of itself.
     *
     * @param anchorPivot Normalised point, 0..1 from the top-left, used as both.
     * @param position Offset from the anchor, in canvas reference pixels.
     * @param size Element size, in canvas reference pixels.
     * @return The element, ready to hand to Scene::add.
     */
    static UIElement at(const glm::vec2& anchorPivot, const glm::vec2& position, const glm::vec2& size) {
        UIElement element;
        element.anchor   = anchorPivot;
        element.pivot    = anchorPivot;
        element.position = position;
        element.size     = size;
        return element;
    }
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::UIElement)
    VKM_F(anchor)
    VKM_F(pivot)
    VKM_F(position)
    VKM_F(size)
    VKM_F(relativeSize)
    VKM_F(visible)
    VKM_F(blocksPointer)
    VKM_F(clipChildren)
VKM_REFLECT_END()
