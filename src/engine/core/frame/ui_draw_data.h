#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "ecs/component/ui/ui_rect.h"
#include "resource/asset/font_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

/**
 * @brief One rectangle of the UI's 2D stream: a solid, a picture or a glyph, drawn as one instance.
 *
 * Screen pixels, top-left origin; colors are straight RGBA. Glyphs (UI_TEXT_MARK) and the image flag
 * are per quad, so solids, text and pictures share a draw call.
 */
struct UIQuad {
    glm::vec4 rect;    ///< Top-left corner (xy) and bottom-right (zw).
    glm::vec4 uv;      ///< Coordinates at those corners: the atlas's for text; 0..1 for a solid.
    glm::vec4 top;     ///< The fill along the top edge.
    glm::vec4 bottom;  ///< The fill along the bottom edge; the top's for a flat quad.
    glm::vec4 border;  ///< Solid only: the border's color.
    glm::vec3 style;   ///< x = corner radius (UI_TEXT_MARK on a glyph), y = border width, both screen px;
                       ///< z = 1 when tinted by its run's image, solid only.
};

constexpr float UI_TEXT_MARK = -1.0f;  ///< A glyph's corner radius: negative, as no solid's is. The backend's
                                       ///< shader prelude names it.

/**
 * @brief A contiguous run of UI quads that share draw state.
 *
 * `font` and `image` are empty when nothing in the run samples them.
 */
struct UIDrawCmd {
    uint32_t firstQuad = 0;
    uint32_t quadCount = 0;

    UIRect clip = {};  ///< Screen-pixel scissor rect, viewport-relative; the whole viewport when unclipped.

    FontHandle    font  = {};
    TextureHandle image = {};
};

/**
 * @brief The backend-agnostic UI overlay for one frame, published on FrameProducts::ui.
 *
 * clear() keeps the vectors' capacity.
 */
struct UIDrawData {
    std::vector<UIQuad>    quads;
    std::vector<UIDrawCmd> commands;

    EntityId pointerTarget{};  ///< The topmost element under the pointer that blocks it, or none. Tells a
                               ///< host whether a click was aimed at the scene behind.

    void clear() {
        quads.clear();
        commands.clear();
        pointerTarget = {};
    }
};

} // namespace Vkm::Engine
