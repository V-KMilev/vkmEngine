#pragma once

#include <cstdint>

#include <imgui.h>

#include "base/math/axes.h"
#include "debug/draw/debug_palette.h"

/// Shared editor visual constants: `*_U32` colors packed for draw lists, ImVec4 ones for widgets.
namespace Vkm::Engine::EditorStyle {

/**
 * @brief DebugPalette::AXES[@p axis], packed for a draw list.
 *
 * @param axis  0, 1 or 2 for X, Y or Z.
 * @param alpha The packed alpha, 0 to 255.
 * @return The packed color.
 */
constexpr ImU32 axisU32(int axis, int alpha = 255) {
    const DebugPalette::Color c = DebugPalette::AXES[axis];
    return IM_COL32(c.r, c.g, c.b, alpha);
}

/**
 * @brief DebugPalette::AXES[@p axis] as a widget color.
 *
 * @param axis 0, 1 or 2 for X, Y or Z.
 * @param lift Added to each 8-bit channel, clamped at 255; a hover state lifts it.
 * @return The widget color, opaque.
 */
constexpr ImVec4 axisVec4(int axis, int lift = 0) {
    const DebugPalette::Color c = DebugPalette::AXES[axis];
    auto channel = [lift](uint8_t v) { return (v + lift > 255 ? 255 : v + lift) / 255.0f; };
    return ImVec4(channel(c.r), channel(c.g), channel(c.b), 1.0f);
}

/// A DebugPalette color, packed for a draw list: a marker in its gizmo's color.
constexpr ImU32 paletteU32(DebugPalette::Color c) {
    return IM_COL32(c.r, c.g, c.b, c.a);
}

inline constexpr ImU32 AXIS_X_U32         = axisU32(0);
inline constexpr ImU32 AXIS_Y_U32         = axisU32(1);
inline constexpr ImU32 AXIS_Z_U32         = axisU32(2);
inline constexpr ImU32 HIGHLIGHT_U32      = paletteU32(DebugPalette::HIGHLIGHT);
inline constexpr ImU32 AXIS_X_FILL_U32    = axisU32(0, 50);
inline constexpr ImU32 AXIS_Y_FILL_U32    = axisU32(1, 50);
inline constexpr ImU32 AXIS_Z_FILL_U32    = axisU32(2, 50);
inline constexpr ImU32 HIGHLIGHT_FILL_U32 = paletteU32(DebugPalette::HIGHLIGHT.faded(60.0f / 255.0f));

/**
 * @brief An sRGB color from its hex code, as the palette is written.
 *
 * @param hex   0xRRGGBB.
 * @param alpha Opacity, 0 to 1.
 * @return The widget color.
 */
constexpr ImVec4 hexColor(uint32_t hex, float alpha = 1.0f) {
    return ImVec4(
        static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
        static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
        static_cast<float>(hex & 0xFF) / 255.0f,
        alpha
    );
}

// Grays, darkest first: field, gap between panels, panel, raised surface, and its hover and press.
inline constexpr ImVec4 FIELD    = hexColor(0x141518);
inline constexpr ImVec4 GAP      = hexColor(0x16171A);
inline constexpr ImVec4 PANEL    = hexColor(0x1E1F23);
inline constexpr ImVec4 RAISED   = hexColor(0x26282D);
inline constexpr ImVec4 HOVERED  = hexColor(0x2C2E34);
inline constexpr ImVec4 PRESSED  = hexColor(0x33363D);
inline constexpr ImVec4 HAIRLINE = hexColor(0x2A2C31);

inline constexpr ImVec4 TEXT           = hexColor(0xE6E7EA);
inline constexpr ImVec4 TEXT_SECONDARY = hexColor(0x8A8D96);

// The one accent: active, selected or prefab-linked. A kind is told by its icon, never a hue.
inline constexpr ImVec4 ACCENT     = hexColor(0x4A9EFF);
inline constexpr ImVec4 ACCENT_HOV = hexColor(0x6BB0FF);

// Viewport overlay background, and the faint edge that separates it from a bright scene.
inline constexpr ImVec4 OVERLAY_BG   = ImVec4(GAP.x, GAP.y, GAP.z, 0.88f);
inline constexpr ImVec4 OVERLAY_EDGE = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);

// The three colors that mean something: it failed or destroys, it may not be what you think, it worked.
inline constexpr ImVec4 DANGER     = hexColor(0xE05A5A);
inline constexpr ImVec4 DANGER_HOV = hexColor(0xE87878);
inline constexpr ImVec4 WARNING    = hexColor(0xF0AD3E);
inline constexpr ImVec4 SUCCESS    = hexColor(0x5CC36F);

inline constexpr float REFERENCE_FONT_SIZE = 14.0f;  ///< The unscaled size the editor's fonts load at; every
                                                     ///< design pixel (px()) is authored against it.

/**
 * @brief The Medium face card titles, section labels and dialog titles are set in.
 *
 * @return Null until the fonts load, or when Medium failed: the text font stands in.
 */
ImFont* headingFont();

/**
 * @brief Name the face headingFont answers with.
 *
 * @param font The loaded Medium face, or null.
 */
void setHeadingFont(ImFont* font);

/// Set what follows in the heading face, at the current size, until ImGui::PopFont.
inline void pushHeadingFont() { ImGui::PushFont(headingFont(), 0.0f); }

/**
 * @brief Design pixels to screen pixels, scaled with the font size and so with the DPI scale.
 *
 * @param units Design pixels, at REFERENCE_FONT_SIZE.
 * @return Screen pixels, rounded to whole ones.
 */
inline float px(float units) {
    // Whole pixels: fractional positions blur text.
    return static_cast<float>(static_cast<int>(ImGui::GetFontSize() * (units / REFERENCE_FONT_SIZE) + 0.5f));
}

/**
 * @brief Width of the label column of a row starting at the cursor.
 *
 * A share of the row, clamped so a narrow panel keeps room for the value.
 *
 * @return Screen pixels.
 */
inline float labelWidth() {
    const float share = ImGui::GetContentRegionAvail().x * 0.4f;
    return share < px(80.0f) ? px(80.0f) : share > px(130.0f) ? px(130.0f) : share;
}

} // namespace Vkm::Engine::EditorStyle

namespace Vkm::Engine {

/**
 * @brief Apply the editor's dark theme (colors, rounding, spacing) to the current ImGui context.
 *
 * Metrics are authored against EditorStyle::REFERENCE_FONT_SIZE, as EditorStyle::px() is.
 *
 * @param scale Display content scale times the user's UI Scale; 1.0f on HiDPI puts 2x text
 *              in 1x chrome.
 */
void applyEditorTheme(float scale);

} // namespace Vkm::Engine
