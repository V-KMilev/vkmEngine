#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "ecs/component/ui/ui_element.h"
#include "resource/asset/font_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

/**
 * @brief One vertex of the UI's 2D triangle stream.
 *
 * `pos` is screen pixels, top-left origin; `color` is straight RGBA. A solid carries
 * its quad's shape on every vertex for the fragment stage. Glyphs (UI_TEXT_MARK) and
 * the `image` flag are per-vertex, so solids, text and pictures share a draw call.
 */
struct UIVertex {
    glm::vec2 pos;
    glm::vec2 uv;      ///< Atlas coordinates for text; the quad's own 0..1 for a solid.
    glm::vec4 color;
    /// Quad width and height (screen px), corner radius (UI_TEXT_MARK on a glyph), border width.
    glm::vec4 shape;
    glm::vec4 border;  ///< Solid only: the border's colour.
    float     image;   ///< Solid only: 1 when tinted by its run's image, 0 when flat.
};

/// A glyph's corner radius: negative, as no solid's is. See GLBackend::shaderConstants.
constexpr float UI_TEXT_MARK = -1.0f;

/**
 * @brief A contiguous run of UI vertices that share draw state.
 *
 * `font` and `image` are empty when nothing in the run samples them.
 */
struct UIDrawCmd {
    uint32_t   firstVertex = 0;
    uint32_t   vertexCount = 0;

    /**
     * @brief Screen-pixel scissor rect, viewport-relative; the whole viewport when unclipped.
     */
    UIRect clip = {};

    FontHandle    font  = {};
    TextureHandle image = {};
};

/**
 * @brief The backend-agnostic UI overlay for one frame, published on FrameContext::ui.
 *
 * clear() keeps the vectors' capacity.
 */
struct UIDrawData {
    std::vector<UIVertex>  vertices;
    std::vector<UIDrawCmd> commands;

    /**
     * @brief The topmost element under the pointer that blocks it, or none.
     *
     * Tells a host whether a click was aimed at the scene behind.
     */
    EntityId pointerTarget{};

    void clear() {
        vertices.clear();
        commands.clear();
        pointerTarget = {};
    }
};

} // namespace Vkm::Engine
