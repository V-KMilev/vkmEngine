#pragma once

// A game's screen as the UI sees it: the UISystem over a viewport, a mouse that points, presses and
// turns its wheel, and the events it fires.

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/frame/ui_draw_data.h"
#include "core/frame/ui_events.h"
#include "ecs/component/ui/ui_build.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/hierarchy_operations.h"
#include "vkm_platform/input/input_handle.h"
#include "resource/asset/font_asset.h"
#include "system/ui/ui_system.h"

#include "harness/world.h"

namespace Vkm::Test {

inline constexpr const char* FONT = "test:font";  ///< The font every UI test draws with: see addFont.

/**
 * @brief A font of known metrics: each glyph inks 8 by 10 baked pixels and advances 10, baked at 10
 *        high (ascent 8, descent -2, lines 12 apart), and "A" before "V" is kerned 3 together.
 *
 * Baked by hand, so a layout's every pixel can be worked out from these numbers.
 */
inline void addFont(ResourceManager& resources, const char* name = FONT) {
    FontAsset font;
    font.pixelHeight = 10.0f;
    font.ascent      = 8.0f;
    font.descent     = -2.0f;
    font.lineHeight  = 12.0f;
    for (FontGlyph& glyph : font.glyphs) {
        glyph.size    = {8.0f, 10.0f};
        glyph.offset  = {1.0f, -8.0f};
        glyph.advance = 10.0f;
        glyph.uvMax   = {0.1f, 0.1f};
    }
    font.glyphs[static_cast<size_t>(FontAsset::glyphIndex(U' '))].size = glm::vec2(0.0f);
    const auto pair = static_cast<uint32_t>(FontAsset::glyphIndex(U'A')) * FontAsset::GLYPH_COUNT
        + static_cast<uint32_t>(FontAsset::glyphIndex(U'V'));
    font.kerning.push_back({pair, -3.0f});
    resources.add(std::move(font), name);
}

/**
 * @brief The UI of one screen, frame by frame, under a mouse the test moves.
 *
 * Canvases laid out at the reference height (1080) draw one reference pixel to one screen pixel.
 */
struct Screen {
    Frame                 frame;
    UISystem              ui;
    Platform::InputHandle mouse;

    std::vector<UIClickEvent> clicks;  ///< Every click fired, in order.
    std::vector<UIValueEvent> values;  ///< Every value a control changed to, in order.

    explicit Screen(uint32_t width = 1920, uint32_t height = 1080) {
        frame.chrome.viewport = {0, 0, width, height};
        addFont(frame.resources);
        frame.events.subscribe<UIClickEvent>([this](const UIClickEvent& e) { clicks.push_back(e); });
        frame.events.subscribe<UIValueEvent>([this](const UIValueEvent& e) { values.push_back(e); });
    }

    Scene& scene() { return frame.scene; }

    /// One frame: the mouse read, the UI laid out and its pointer settled, its events delivered.
    const UIDrawData& show() {
        frame.input.update(mouse, frame.chrome);
        mouse.resetScrollDelta();
        ui.update(frame.ctx);
        frame.events.flush();
        return *frame.ctx.ui;
    }

    void pointAt(const glm::vec2& at) { mouse.moveTo(at.x, at.y); }
    void press() { mouse.setButton(Platform::MouseButton::Left, true); }
    void release() { mouse.setButton(Platform::MouseButton::Left, false); }
    void turnWheel(float notches) { mouse.addScroll(notches); }

    /// Press and let go at @p at, a frame each.
    void click(const glm::vec2& at) {
        pointAt(at);
        press();
        show();
        release();
        show();
    }

    /// Where @p entity was laid out last frame, in screen pixels.
    UIRect rectOf(EntityId entity) { return frame.scene.get<UIElement>(entity).screenRect; }
    glm::vec2 middleOf(EntityId entity) { return rectOf(entity).pos + rectOf(entity).size * 0.5f; }

    /// A plain rectangle of @p color at @p at under @p parent.
    EntityId image(EntityId parent, const UIElement& at, const glm::vec4& color = glm::vec4(1.0f)) {
        return UI::panel(frame.scene, parent, at, color);
    }

    /// An element with nothing on it, at @p at under @p parent: a layout box.
    EntityId box(EntityId parent, const UIElement& at) {
        const EntityId id = frame.scene.createEntity();
        frame.scene.add(id, at);
        HierarchyOperations::setParent(frame.scene, id, parent);
        return id;
    }
};

/// An element of @p size whose top left sits @p at from its parent's.
inline UIElement topLeft(
    const glm::vec2& at,
    const glm::vec2& size
) { return UIElement::at({0.0f, 0.0f}, at, size); }

} // namespace Vkm::Test
