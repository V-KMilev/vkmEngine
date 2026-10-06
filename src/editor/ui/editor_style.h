#pragma once

#include <imgui.h>

/**
 * @brief Shared editor visual constants.
 *
 * Drawlist code uses the `*_U32` packed colors; widget styling the ImVec4 ones.
 */
namespace Vkm::Engine::EditorStyle {

// Axis colors - ImDrawList packed form.
inline constexpr ImU32 AXIS_X_U32      = IM_COL32(220,  60,  60, 255);
inline constexpr ImU32 AXIS_Y_U32      = IM_COL32( 80, 190,  60, 255);
inline constexpr ImU32 AXIS_Z_U32      = IM_COL32( 60, 100, 220, 255);
inline constexpr ImU32 HIGHLIGHT_U32   = IM_COL32(255, 210,  50, 255);
inline constexpr ImU32 AXIS_X_FILL_U32 = IM_COL32(220,  60,  60,  50);
inline constexpr ImU32 AXIS_Y_FILL_U32 = IM_COL32( 80, 190,  60,  50);
inline constexpr ImU32 AXIS_Z_FILL_U32 = IM_COL32( 60, 100, 220,  50);

// Axis colors - ImGui widget (ImVec4) form, with hover variants.
inline const ImVec4 AXIS_X      = ImVec4(0.86f, 0.24f, 0.24f, 1.00f);
inline const ImVec4 AXIS_Y      = ImVec4(0.31f, 0.75f, 0.24f, 1.00f);
inline const ImVec4 AXIS_Z      = ImVec4(0.24f, 0.39f, 0.86f, 1.00f);
inline const ImVec4 AXIS_X_HOV  = ImVec4(0.94f, 0.34f, 0.34f, 1.00f);
inline const ImVec4 AXIS_Y_HOV  = ImVec4(0.41f, 0.85f, 0.34f, 1.00f);
inline const ImVec4 AXIS_Z_HOV  = ImVec4(0.34f, 0.49f, 0.94f, 1.00f);

// Accent for active/selected affordances.
inline const ImVec4 ACCENT      = ImVec4(0.29f, 0.62f, 1.00f, 1.00f);
inline const ImVec4 ACCENT_HOV  = ImVec4(0.42f, 0.71f, 1.00f, 1.00f);

inline const ImVec4 HEADER_TEXT = ImVec4(0.78f, 0.85f, 0.97f, 1.00f);

// Component-card header (collapsing).
inline const ImVec4 CARD_HEADER     = ImVec4(0.175f, 0.190f, 0.225f, 1.00f);
inline const ImVec4 CARD_HEADER_HOV = ImVec4(0.235f, 0.255f, 0.300f, 1.00f);
inline const ImVec4 CARD_HEADER_ACT = ImVec4(0.215f, 0.235f, 0.280f, 1.00f);

// Viewport overlay background, and the faint edge that separates it from a bright scene.
inline const ImVec4 OVERLAY_BG   = ImVec4(0.10f, 0.105f, 0.125f, 0.86f);
inline const ImVec4 OVERLAY_EDGE = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);

// Navigation-gizmo disc (drawlist).
inline constexpr ImU32 NAV_DISC_U32 = IM_COL32(20, 20, 22, 160);
inline constexpr ImU32 NAV_RING_U32 = IM_COL32(50, 50, 55, 200);
inline constexpr ImU32 NAV_LABEL_U32 = IM_COL32(16, 16, 20, 255);  // letter on an axis dot

// Timeline (Animation panel) drawlist palette.
inline constexpr ImU32 TIMELINE_BG_U32    = IM_COL32( 18,  18,  20, 255);
inline constexpr ImU32 TIMELINE_TICK_U32  = IM_COL32( 90,  90,  95, 255);
inline constexpr ImU32 TIMELINE_LABEL_U32 = IM_COL32(150, 150, 155, 255);
inline constexpr ImU32 TIMELINE_LANE_U32  = IM_COL32( 45,  45,  50, 255);
inline constexpr ImU32 TIMELINE_GHOST_U32 = IM_COL32( 25,  25,  28, 200);

// Destructive actions (delete / remove).
inline const ImVec4 DANGER      = ImVec4(0.86f, 0.34f, 0.34f, 1.00f);

// Cautions.
inline const ImVec4 WARNING     = ImVec4(0.95f, 0.68f, 0.25f, 1.00f);

// Positive states.
inline const ImVec4 SUCCESS     = ImVec4(0.40f, 0.80f, 0.45f, 1.00f);

// Toast backgrounds, per kind (dark, readable under white text).
inline const ImVec4 TOAST_ERROR_BG   = ImVec4(0.55f, 0.18f, 0.18f, 1.00f);
inline const ImVec4 TOAST_WARNING_BG = ImVec4(0.55f, 0.42f, 0.10f, 1.00f);
inline const ImVec4 TOAST_INFO_BG    = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);

/**
 * @brief The card accent registry: every card hue in the editor, named once.
 *
 * One hue per idea, not per place: MAT_TEXTURE is what a texture wears wherever it is drawn.
 */
namespace Accent {
    inline const ImVec4 TRANSFORM   = AXIS_Z;
    inline const ImVec4 MESH        = AXIS_Y;
    inline const ImVec4 LIGHT       = ImVec4(1.00f, 0.80f, 0.22f, 1.0f);  // gold
    inline const ImVec4 CAMERA      = ImVec4(0.30f, 0.78f, 0.80f, 1.0f);  // cyan
    inline const ImVec4 ANIM        = ImVec4(0.64f, 0.44f, 0.86f, 1.0f);  // purple
    inline const ImVec4 HIERARCHY   = ImVec4(0.55f, 0.58f, 0.62f, 1.0f);  // neutral
    inline const ImVec4 PHYSICS     = ImVec4(0.36f, 0.78f, 0.45f, 1.0f);  // green
    inline const ImVec4 COLLIDER    = ImVec4(0.25f, 0.65f, 0.40f, 1.0f);  // deep green
    inline const ImVec4 PROBE       = ImVec4(0.30f, 0.62f, 0.92f, 1.0f);  // blue
    inline const ImVec4 ENV         = ImVec4(0.45f, 0.66f, 0.95f, 1.0f);  // sky blue
    inline const ImVec4 SCRIPT      = ImVec4(0.85f, 0.45f, 0.58f, 1.0f);  // rose
    inline const ImVec4 UI          = ImVec4(0.95f, 0.62f, 0.30f, 1.0f);  // amber
    inline const ImVec4 PREFAB      = ImVec4(0.52f, 0.45f, 0.95f, 1.0f);  // indigo
    inline const ImVec4 AUDIO       = ImVec4(0.88f, 0.38f, 0.80f, 1.0f);  // magenta (not SCRIPT's rose)

    inline const ImVec4 MAT_BASE    = ImVec4(0.90f, 0.55f, 0.25f, 1.0f);  // warm
    inline const ImVec4 MAT_TEXTURE = ImVec4(0.28f, 0.74f, 0.74f, 1.0f);  // teal
    inline const ImVec4 MAT_GLASS   = ImVec4(0.55f, 0.85f, 0.65f, 1.0f);  // mint
    inline const ImVec4 MAT_COAT    = ImVec4(0.45f, 0.62f, 0.92f, 1.0f);  // light blue
    inline const ImVec4 MAT_ANISO   = ImVec4(0.72f, 0.50f, 0.90f, 1.0f);  // lilac (not ANIM's purple)
    inline const ImVec4 MAT_SSS     = ImVec4(0.88f, 0.45f, 0.55f, 1.0f);  // pink
    inline const ImVec4 MAT_SHEEN   = ImVec4(1.00f, 0.72f, 0.38f, 1.0f);  // brass (not LIGHT's gold)

    inline const ImVec4 QUALITY     = ImVec4(0.55f, 0.62f, 0.75f, 1.0f);  // neutral-cool
    inline const ImVec4 EFFECT      = ImVec4(0.36f, 0.60f, 0.92f, 1.0f);  // effect blue

} // namespace Accent

/**
 * @brief The size the editor's text and icon fonts are loaded at, unscaled.
 *
 * Every design pixel (px(), the theme's metrics) is authored against it.
 */
inline constexpr float REFERENCE_FONT_SIZE = 15.0f;

/**
 * @brief Font-relative pixel metric.
 *
 * Scales design pixels with the loaded font size, and so with the DPI scale.
 *
 * @param units Design pixels.
 * @return Screen pixels, rounded to whole ones.
 */
inline float px(float units) {
    // Whole pixels: fractional positions blur text.
    return static_cast<float>(static_cast<int>(ImGui::GetFontSize() * (units / REFERENCE_FONT_SIZE) + 0.5f));
}

/// Width reserved for aligned property labels.
inline float labelWidth() { return px(100.0f); }

/// Side of an icon button on a viewport overlay strip.
inline float overlayButton()   { return px(26.0f); }
/// A strip's inner padding.
inline float overlayPad()      { return px(5.0f); }
/// Spacing between adjacent buttons in a strip.
inline float overlayGap()      { return px(4.0f); }
/// Spacing between groups of buttons in a strip.
inline float overlayGroupGap() { return px(10.0f); }
/// Inset from the viewport edge a strip floats at.
inline float overlayInset()    { return px(8.0f); }
/// Width of each dropdown on the view bar.
inline float overlayComboWidth() { return px(116.0f); }
/// Corner rounding of a strip.
inline float overlayRounding() { return px(6.0f); }

/**
 * @brief What a strip adds around its buttons on each axis.
 *
 * Padding and child border, both sides. The border is read from the style, which
 * ScaleAllSizes already scaled, not through px().
 *
 * @return Screen pixels.
 */
inline float overlayStripChrome() {
    return overlayPad() * 2.0f + ImGui::GetStyle().ChildBorderSize * 2.0f;
}

/// Outer height of a strip: one row of buttons and its chrome.
inline float overlayStripHeight() { return overlayButton() + overlayStripChrome(); }

} // namespace Vkm::Engine::EditorStyle
