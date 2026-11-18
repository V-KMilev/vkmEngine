#pragma once

#include <cstdint>
#include <string>

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Root of a screen-space UI layer.
 *
 * Descendant UIElements lay out against a reference height, scaled uniformly to the
 * live viewport. Several canvases draw in ascending sortOrder.
 */
struct UICanvas {
    static constexpr float REFERENCE_HEIGHT = 1080.0f;  ///< The screen height a canvas is authored at unless
                                                        ///< it says otherwise, and anything sized as one.

    /**
     * @brief How the canvas maps authored pixels to on-screen pixels.
     */
    enum class ScaleMode : uint8_t {
        Fixed,           ///< One authored pixel equals one screen pixel.
        ScaleWithHeight, ///< Scale uniformly by viewportHeight / referenceHeight.
        Count            ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
    };

    float     referenceHeight = REFERENCE_HEIGHT;  ///< Authoring height in pixels; ScaleWithHeight scales
                                                   ///< against it.
    ScaleMode scaleMode       = ScaleMode::ScaleWithHeight;
    int32_t   sortOrder       = 0;                 ///< Draw order across canvases; higher draws on top.
    bool      visible         = true;              ///< Skip the canvas and its whole subtree when false.

    bool pausesGame = false;  ///< While it shows, the world stands still and the cursor is free: a pause
                              ///< menu, an end screen. Only while the world runs, and offline: a session's
                              ///< world is the server's, so only the cursor is freed. Shown at load, it holds
                              ///< the world before any behavior starts, so a behavior shows it.

    std::string toggleAction;  ///< An action whose press shows or hides it, the world paused or not: "Menu"
                               ///< for a pause menu, opened and closed with no code. Empty for none.

    /**
     * @brief Screen pixels a pixel of @p canvas spans, drawn into a view @p viewHeight pixels tall.
     *
     * @param canvas     The canvas.
     * @param viewHeight The view's height, in screen pixels.
     * @return The scale its layout is drawn at.
     */
    static float scaleIn(const UICanvas& canvas, float viewHeight) {
        const bool scaled = canvas.scaleMode == ScaleMode::ScaleWithHeight && canvas.referenceHeight > 0.0f;
        return scaled ? viewHeight / canvas.referenceHeight : 1.0f;
    }
};

/**
 * @brief Whether a UICanvas sits strictly above @p id in the hierarchy.
 *
 * That is whether UISystem's layout walk reaches it at all: an element outside every
 * canvas, or on the canvas entity itself, is never drawn or hit-tested. Bounded by
 * HierarchyOperations::MAX_DEPTH.
 *
 * @param scene Scene to walk.
 * @param id Entity to answer for.
 * @return true when some ancestor of @p id carries a UICanvas.
 */
bool hasCanvasAncestor(const Scene& scene, EntityId id);

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::UICanvas::ScaleMode, "Fixed", "ScaleWithHeight")

VKM_REFLECT_BEGIN(::Vkm::Engine::UICanvas)
    VKM_F(referenceHeight, 1.0f, 8192.0f, "The height it is laid out at; Scale With Height scales to it")
    VKM_F(scaleMode)
    VKM_F(sortOrder, -1000.0f, 1000.0f, "Higher draws on top across canvases")
    VKM_F(visible)
    VKM_F(pausesGame, "While it shows, the world stands still and the cursor is free")
    VKM_F(toggleAction, "An action whose press shows or hides it, such as Menu")
VKM_REFLECT_END()
