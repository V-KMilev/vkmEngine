#pragma once

#include <cstdint>

#include "core/reflect.h"

namespace Engine {

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

} // namespace Engine

VKM_ENUM_NAMES(::Engine::UICanvas::ScaleMode, "Fixed", "ScaleWithHeight")

VKM_REFLECT_BEGIN(::Engine::UICanvas)
    VKM_F(referenceHeight),
    VKM_F(scaleMode),
    VKM_F(sortOrder),
    VKM_F(visible)
VKM_REFLECT_END()
