#pragma once

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"
#include "ecs/component/ui/ui_rect.h"

namespace Vkm::Engine {

/**
 * @brief The 2D rect of a UI node - the screen-space analog of Transform.
 *
 * The element's `pivot` point is pinned to its parent rect's `anchor` point (both
 * normalized, top-left origin). `position` and `size` are in canvas reference pixels.
 */
struct UIElement {
    glm::vec2 anchor   = {0.5f, 0.5f};      ///< Parent anchor point, normalized (0,0 = top-left, 1,1 =
                                            ///< bottom-right).
    glm::vec2 pivot    = {0.5f, 0.5f};      ///< Element pivot, normalized; the point placed at the anchor.
    glm::vec2 position = {0.0f, 0.0f};      ///< Offset from the anchor, in canvas reference pixels.
    glm::vec2 size     = {100.0f, 100.0f};  ///< Element size, in canvas reference pixels, added to
                                            ///< relativeSize's share.

    glm::vec2 relativeSize = {0.0f, 0.0f};  ///< The share of the parent's size this element takes, added to
                                            ///< `size`. (1, 1) with size (-40, -40) at a centered anchor is a
                                            ///< 20 inset on every side. A resolved size below zero is zero.

    bool visible = true;  ///< Skip this element and its whole subtree when false.

    float opacity = 1.0f;  ///< How opaque this element and its whole subtree draw, 0 to 1, multiplied down
                           ///< the tree onto every color's alpha: what a panel fades by without touching each
                           ///< color under it. At 0 the subtree is skipped as when not visible, and takes no
                           ///< clicks.

    bool blocksPointer = true;  ///< Whether this element stops a pointer reaching what is behind it.
                                ///< Consulted only on a UIImage or UIButton; clear it for a click-through
                                ///< overlay.

    bool clipChildren = false;  ///< Whether this element's rect bounds what its descendants may draw: not its
                                ///< own text, whose glyphs draw under its parent's clip, so a text kept
                                ///< inside a box sits under a clipping parent. Off by default, since it costs
                                ///< a scissor change. Bounds the pointer as well as the pixels. A UIScroll
                                ///< clips whether or not this is set.

    UIRect screenRect = {};  ///< Resolved rect in screen pixels, written each frame.

    /**
     * @brief An element pinned to one normalized point of its parent by the matching point of itself.
     *
     * @param anchorPivot Normalized point, 0..1 from the top-left, used as both.
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
    VKM_F(anchor, 0.0f, 1.0f, "The parent's point it hangs from, 0 to 1 from the top left")
    VKM_F(pivot, 0.0f, 1.0f, "Its own point placed at the anchor")
    VKM_F(position, "Offset from the anchor, in reference pixels")
    VKM_F(size, -8192.0f, 8192.0f, "Reference pixels, added to Relative Size's share")
    VKM_F(relativeSize, 0.0f, 1.0f, "Its share of the parent's size, added to Size")
    VKM_F(visible, "Off hides it and everything under it")
    VKM_F(opacity, 0.0f, 1.0f, "How opaque it and its subtree draw; at 0 it takes no clicks")
    VKM_F(blocksPointer, "Stops a click reaching what is behind it")
    VKM_F(clipChildren, "Bounds what is under it, drawn and clicked, to its rect")
VKM_REFLECT_END()
