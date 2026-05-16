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

// Accent used for active/selected affordances (toolbar tool, snap on, ...).
inline const ImVec4 ACCENT      = ImVec4(0.26f, 0.52f, 0.88f, 1.00f);

// Panel title text (Hierarchy / Inspector / Bottom section headers).
inline const ImVec4 HEADER_TEXT = ImVec4(0.70f, 0.78f, 0.90f, 1.00f);

// Translucent background for floating viewport overlays.
inline const ImVec4 OVERLAY_BG  = ImVec4(0.11f, 0.11f, 0.12f, 0.85f);

// Danger hover (delete buttons).
inline const ImVec4 DANGER_HOV  = ImVec4(0.72f, 0.26f, 0.26f, 1.00f);

// Width reserved for aligned property labels in inspector-style rows.
inline constexpr float LABEL_WIDTH = 100.0f;

} // namespace Engine::EditorStyle
