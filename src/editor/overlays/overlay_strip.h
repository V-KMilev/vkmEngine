#pragma once

#include <imgui.h>

#include "ui/style/editor_style.h"

/// The strips that float over the viewport: their sizes, and the frame they draw in.
namespace Vkm::Engine::OverlayStrip {

/// Side of an icon button on a viewport overlay strip.
inline float button()   { return EditorStyle::px(26.0f); }
/// A strip's inner padding.
inline float pad()      { return EditorStyle::px(5.0f); }
/// Spacing between adjacent buttons in a strip.
inline float gap()      { return EditorStyle::px(4.0f); }
/// Spacing between groups of buttons in a strip.
inline float groupGap() { return EditorStyle::px(10.0f); }
/// Inset from the viewport edge a strip floats at.
inline float inset()    { return EditorStyle::px(8.0f); }
/// Width of each dropdown on the view bar.
inline float comboWidth() { return EditorStyle::px(116.0f); }
/// Corner rounding of a strip.
inline float rounding() { return EditorStyle::px(6.0f); }

/**
 * @brief What a strip adds around its buttons on each axis: padding and child border, both sides.
 *
 * The border is the style's, which ScaleAllSizes already scaled, not read through px().
 *
 * @return Screen pixels.
 */
inline float chrome() {
    return pad() * 2.0f + ImGui::GetStyle().ChildBorderSize * 2.0f;
}

/// Outer height of a strip: one row of buttons and its chrome.
inline float height() { return button() + chrome(); }

/**
 * @brief Begin one of the strips that float over the viewport.
 *
 * End with end whatever this returns.
 *
 * @param id    ImGui id.
 * @param size  Outer size; 0 on an axis fits the contents.
 * @return Whether its contents are drawn.
 */
bool begin(const char* id, ImVec2 size);

/// End a strip begin began.
void end();

} // namespace Vkm::Engine::OverlayStrip
