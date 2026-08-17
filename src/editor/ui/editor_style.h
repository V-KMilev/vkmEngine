#pragma once

#include <imgui.h>

namespace Engine::EditorStyle {

/**
 * @brief Shared editor visual constants.
 *
 * Single source of truth for colors and metrics used by panels, widgets
 * and the gizmo. Drawlist code uses the `*_U32` packed colors; ImGui
 * widget styling uses the ImVec4 variants.
 */

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

// Accent for active/selected affordances. A single vivid blue drives the
// whole UI - changing this one value re-tints every accent surface.
inline const ImVec4 ACCENT      = ImVec4(0.29f, 0.62f, 1.00f, 1.00f);
inline const ImVec4 ACCENT_HOV  = ImVec4(0.42f, 0.71f, 1.00f, 1.00f);

inline const ImVec4 HEADER_TEXT = ImVec4(0.78f, 0.85f, 0.97f, 1.00f);

// Component-card header (collapsing). Hoisted here so the card look stays
// in lockstep with the theme instead of being hardcoded in the widget.
inline const ImVec4 CARD_HEADER     = ImVec4(0.175f, 0.190f, 0.225f, 1.00f);
inline const ImVec4 CARD_HEADER_HOV = ImVec4(0.235f, 0.255f, 0.300f, 1.00f);
inline const ImVec4 CARD_HEADER_ACT = ImVec4(0.215f, 0.235f, 0.280f, 1.00f);

// Background for floating viewport overlays.
inline const ImVec4 OVERLAY_BG  = ImVec4(0.10f, 0.10f, 0.12f, 0.88f);

// Navigation-gizmo ball (drawlist).
inline constexpr ImU32 NAV_DISC_U32 = IM_COL32(20, 20, 22, 160);
inline constexpr ImU32 NAV_RING_U32 = IM_COL32(50, 50, 55, 200);

// Timeline (Bottom panel) drawlist palette.
inline constexpr ImU32 TIMELINE_BG_U32    = IM_COL32( 18,  18,  20, 255);
inline constexpr ImU32 TIMELINE_TICK_U32  = IM_COL32( 90,  90,  95, 255);
inline constexpr ImU32 TIMELINE_LABEL_U32 = IM_COL32(150, 150, 155, 255);
inline constexpr ImU32 TIMELINE_LANE_U32  = IM_COL32( 45,  45,  50, 255);
inline constexpr ImU32 TIMELINE_GHOST_U32 = IM_COL32( 25,  25,  28, 200);

// Destructive actions (delete / remove). One color, used everywhere.
inline const ImVec4 DANGER      = ImVec4(0.86f, 0.34f, 0.34f, 1.00f);

// Cautions (unsaved overwrite, keybind conflicts, mid FPS). One orange - the
// editor previously grew three near-identical ad-hoc ones.
inline const ImVec4 WARNING     = ImVec4(0.95f, 0.68f, 0.25f, 1.00f);

// Positive states (healthy FPS, success toasts).
inline const ImVec4 SUCCESS     = ImVec4(0.40f, 0.80f, 0.45f, 1.00f);

// Toast backgrounds, per kind (dark, readable under white text).
inline const ImVec4 TOAST_ERROR_BG   = ImVec4(0.55f, 0.18f, 0.18f, 1.00f);
inline const ImVec4 TOAST_WARNING_BG = ImVec4(0.55f, 0.42f, 0.10f, 1.00f);
inline const ImVec4 TOAST_INFO_BG    = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);

/**
 * @brief The card accent registry: every card hue in the editor, named once.
 *
 * The left accent strip / guide line is how the eye groups a card; panels used
 * to re-declare these locally, which let two pairs collide by accident (Light
 * vs Sheen, Anim vs Anisotropy - now intentionally distinct). Component cards
 * first, Material Editor groups second, Render Settings groups last.
 */
namespace Accent {
    inline const ImVec4 Transform  = AXIS_Z;
    inline const ImVec4 Mesh       = AXIS_Y;
    inline const ImVec4 Light      = ImVec4(1.00f, 0.80f, 0.22f, 1.0f);  // gold
    inline const ImVec4 Camera     = ImVec4(0.30f, 0.78f, 0.80f, 1.0f);  // cyan
    inline const ImVec4 Anim       = ImVec4(0.64f, 0.44f, 0.86f, 1.0f);  // purple
    inline const ImVec4 Hierarchy  = ImVec4(0.55f, 0.58f, 0.62f, 1.0f);  // neutral
    inline const ImVec4 Physics    = ImVec4(0.36f, 0.78f, 0.45f, 1.0f);  // green
    inline const ImVec4 Collider   = ImVec4(0.25f, 0.65f, 0.40f, 1.0f);  // deep green
    inline const ImVec4 Probe      = ImVec4(0.30f, 0.62f, 0.92f, 1.0f);  // blue
    inline const ImVec4 Env        = ImVec4(0.45f, 0.66f, 0.95f, 1.0f);  // sky blue
    inline const ImVec4 Script     = ImVec4(0.85f, 0.45f, 0.58f, 1.0f);  // rose
    inline const ImVec4 UI         = ImVec4(0.95f, 0.62f, 0.30f, 1.0f);  // amber
    inline const ImVec4 Prefab     = ImVec4(0.52f, 0.45f, 0.95f, 1.0f);  // indigo

    inline const ImVec4 MatBase    = ImVec4(0.90f, 0.55f, 0.25f, 1.0f);  // warm
    inline const ImVec4 MatSurface = ImVec4(0.28f, 0.74f, 0.74f, 1.0f);  // teal
    inline const ImVec4 MatCoat    = ImVec4(0.45f, 0.62f, 0.92f, 1.0f);  // light blue
    inline const ImVec4 MatAniso   = ImVec4(0.72f, 0.50f, 0.90f, 1.0f);  // lilac (not Anim's purple)
    inline const ImVec4 MatSSS     = ImVec4(0.88f, 0.45f, 0.55f, 1.0f);  // pink
    inline const ImVec4 MatSheen   = ImVec4(1.00f, 0.72f, 0.38f, 1.0f);  // brass (not Light's gold)
    inline const ImVec4 MatVolume  = ImVec4(0.55f, 0.85f, 0.65f, 1.0f);  // mint
    inline const ImVec4 MatTexture = AXIS_Y;

    inline const ImVec4 Quality    = ImVec4(0.55f, 0.62f, 0.75f, 1.0f);  // neutral-cool
    inline const ImVec4 Effect     = ImVec4(0.36f, 0.60f, 0.92f, 1.0f);  // effect blue
} // namespace Accent

/**
 * @brief Font-relative pixel metric.
 *
 * @p units are design pixels at the 15 px reference font; the result scales
 * with the loaded font size (and therefore the content/DPI scale), so fixed
 * layouts keep their proportions on any display.
 */
inline float px(float units) {
    // Rounded to whole pixels: fractional positions land text between pixels
    // and the AA smear reads as blur.
    return static_cast<float>(static_cast<int>(ImGui::GetFontSize() * (units / 15.0f) + 0.5f));
}

// Width reserved for aligned property labels. Font-relative so the column
// scales with the loaded font size / DPI (~100 px at the default 15 px font).
inline float labelWidth() { return static_cast<float>(static_cast<int>(ImGui::GetFontSize() * 6.7f + 0.5f)); }

} // namespace Engine::EditorStyle
