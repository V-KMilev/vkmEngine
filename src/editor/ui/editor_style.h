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

// Axis colors (X red, Y green, Z blue) - ImDrawList packed form.
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

// Accent used for active/selected affordances (toolbar tool, snap on,
// active preset, slider grabs, selection, ...). A single vivid blue drives
// the whole UI - changing this one value re-tints every accent surface.
inline const ImVec4 ACCENT      = ImVec4(0.29f, 0.62f, 1.00f, 1.00f);
inline const ImVec4 ACCENT_HOV  = ImVec4(0.42f, 0.71f, 1.00f, 1.00f);

// Panel / section title text.
inline const ImVec4 HEADER_TEXT = ImVec4(0.78f, 0.85f, 0.97f, 1.00f);

// Component-card header (collapsing). Hoisted here so the card look stays
// in lockstep with the theme instead of being hardcoded in the widget.
inline const ImVec4 CARD_HEADER     = ImVec4(0.175f, 0.190f, 0.225f, 1.00f);
inline const ImVec4 CARD_HEADER_HOV = ImVec4(0.235f, 0.255f, 0.300f, 1.00f);
inline const ImVec4 CARD_HEADER_ACT = ImVec4(0.215f, 0.235f, 0.280f, 1.00f);

// Translucent background for floating viewport overlays.
inline const ImVec4 OVERLAY_BG  = ImVec4(0.10f, 0.10f, 0.12f, 0.88f);

// Destructive actions (delete / remove). One color, used everywhere.
inline const ImVec4 DANGER      = ImVec4(0.86f, 0.34f, 0.34f, 1.00f);

// Width reserved for aligned property labels in inspector-style rows.
inline constexpr float LABEL_WIDTH = 100.0f;

} // namespace Engine::EditorStyle
