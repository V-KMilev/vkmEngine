#pragma once

#include <cstdint>

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Root of a screen-space UI layer.
 *
 * A canvas turns the render viewport into the coordinate space its descendant
 * UIElements lay out in. Layouts are authored against a fixed reference height;
 * at runtime the canvas derives a uniform scale from the live viewport so the
 * layout keeps its proportions across window sizes. Put a UICanvas on a root
 * entity and parent UIElement entities under it through the normal entity
 * hierarchy. Several canvases can coexist (e.g. a HUD and a menu); they draw in
 * ascending sortOrder.
 */
struct UICanvas {
    /**
     * @brief How the canvas maps authored pixels to on-screen pixels.
     */
    enum class ScaleMode : uint8_t {
        Fixed,           ///< One authored pixel equals one screen pixel.
        ScaleWithHeight, ///< Scale uniformly by viewportHeight / referenceHeight.
        Count            ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
    };

    float     referenceHeight = 1080.0f;                     ///< Authoring height in pixels; ScaleWithHeight scales against it.
    ScaleMode scaleMode       = ScaleMode::ScaleWithHeight;  ///< Authored-pixel to screen-pixel mapping.
    int32_t   sortOrder       = 0;                           ///< Draw order across canvases; higher draws on top.
    bool      visible         = true;                        ///< Skip the canvas and its whole subtree when false.
};

/**
 * @brief Whether a UICanvas sits strictly above @p id in the hierarchy.
 *
 * The one definition of the rule that decides whether a UI entity is drawn at
 * all. UISystem seeds its layout walk from each canvas and descends through
 * HierarchyOperations::forEachChild, so it reaches a canvas's descendants and
 * never the canvas entity itself: an element outside every canvas gets no
 * screen rect, no draw command and no hit test, and so does a UIElement placed
 * on the canvas entity. Strictly above, for that second case - answering "yes"
 * for the canvas's own entity would call drawable a thing the walk never
 * visits.
 *
 * Both the Inspector's UI Element card and the interactive reparent ask it, and
 * they have to agree: one says the element will not appear and the other says
 * nothing, over the same entity. Bounded by HierarchyOperations::MAX_DEPTH, the
 * depth the resolve pass itself stops at.
 *
 * @param scene The scene to walk.
 * @param id Entity to answer for.
 * @return true when some ancestor of @p id carries a UICanvas.
 */
bool hasCanvasAncestor(const Scene& scene, EntityId id);

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::UICanvas::ScaleMode, "Fixed", "ScaleWithHeight")

VKM_REFLECT_BEGIN(::Vkm::Engine::UICanvas)
    VKM_F(referenceHeight),
    VKM_F(scaleMode),
    VKM_F(sortOrder),
    VKM_F(visible)
VKM_REFLECT_END()
