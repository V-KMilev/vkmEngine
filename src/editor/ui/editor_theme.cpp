#include "ui/editor_theme.h"
#include "ui/editor_style.h"

#include <imgui.h>

namespace Engine {

// Bolder, modern dark theme. A cool-tinted neutral ramp gives panels a sense
// of elevation (window < child < popup, frames recessed), and a single vivid
// accent (EditorStyle::ACCENT) drives every interactive/selected surface so
// the whole editor reads as one designed system. Generous spacing/rounding
// for breathing room in a feature-dense UI.
void applyEditorTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 6.0f;
    style.WindowBorderSize  = 1.0f;
    style.WindowPadding     = ImVec2(10, 8);
    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    style.FrameRounding     = 5.0f;
    style.FramePadding      = ImVec2(8, 4);  // comfortable, not chunky
    style.FrameBorderSize   = 0.0f;
    style.PopupRounding     = 6.0f;
    style.GrabRounding      = 4.0f;
    style.GrabMinSize       = 11.0f;
    style.ItemSpacing       = ImVec2(9, 7);
    style.ItemInnerSpacing  = ImVec2(6, 5);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 13.0f;
    style.ScrollbarRounding = 8.0f;
    style.TabRounding       = 6.0f;
    // Docked panels are child regions tiled edge-to-edge; rounded corners
    // leave triangular gaps at the seams. Square them (floating windows
    // keep WindowRounding). A 1px child border now reads as panel elevation.
    style.ChildRounding     = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.CellPadding       = ImVec2(7, 4);
    style.SeparatorTextBorderSize = 2.0f;
    style.DisabledAlpha     = 0.45f;

    const ImVec4 A   = EditorStyle::ACCENT;
    const ImVec4 AH  = EditorStyle::ACCENT_HOV;
    auto aA = [](ImVec4 v, float a) { v.w = a; return v; };

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.46f, 0.48f, 0.55f, 1.00f);
    c[ImGuiCol_WindowBg]              = ImVec4(0.105f, 0.110f, 0.125f, 1.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(0.125f, 0.130f, 0.150f, 1.00f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.145f, 0.155f, 0.180f, 0.98f);
    c[ImGuiCol_Border]                = ImVec4(0.32f, 0.35f, 0.44f, 0.45f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.070f, 0.075f, 0.090f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.175f, 0.195f, 0.235f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.225f, 0.250f, 0.305f, 1.00f);
    c[ImGuiCol_TitleBg]               = ImVec4(0.085f, 0.090f, 0.105f, 1.00f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.130f, 0.140f, 0.170f, 1.00f);
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.130f, 0.140f, 0.165f, 1.00f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.070f, 0.075f, 0.090f, 0.50f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.33f, 0.40f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.40f, 0.44f, 0.52f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = aA(A, 0.85f);
    c[ImGuiCol_CheckMark]             = AH;
    c[ImGuiCol_SliderGrab]            = A;
    c[ImGuiCol_SliderGrabActive]      = AH;
    c[ImGuiCol_Button]                = ImVec4(0.185f, 0.200f, 0.240f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = aA(A, 0.85f);
    c[ImGuiCol_ButtonActive]          = aA(A, 1.00f);
    c[ImGuiCol_Header]                = ImVec4(0.205f, 0.235f, 0.300f, 1.00f);
    c[ImGuiCol_HeaderHovered]         = aA(A, 0.55f);
    c[ImGuiCol_HeaderActive]          = aA(A, 0.75f);
    c[ImGuiCol_Separator]             = ImVec4(0.26f, 0.29f, 0.36f, 0.55f);
    c[ImGuiCol_SeparatorHovered]      = aA(A, 0.70f);
    c[ImGuiCol_Tab]                   = ImVec4(0.135f, 0.145f, 0.170f, 1.00f);
    c[ImGuiCol_TabHovered]            = aA(A, 0.70f);
    c[ImGuiCol_TabSelected]           = ImVec4(0.230f, 0.275f, 0.360f, 1.00f);
    c[ImGuiCol_PlotLines]             = AH;
    c[ImGuiCol_PlotHistogram]         = aA(AH, 0.85f);
    c[ImGuiCol_TableHeaderBg]         = ImVec4(0.150f, 0.160f, 0.190f, 1.00f);
    c[ImGuiCol_TableBorderStrong]     = ImVec4(0.26f, 0.29f, 0.36f, 1.00f);
    c[ImGuiCol_TableBorderLight]      = ImVec4(0.19f, 0.21f, 0.26f, 1.00f);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.025f);

    // Fill the remaining slots so nothing falls back to StyleColorsDark's
    // teal defaults (these are what made stray bits look "un-themed").
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TextSelectedBg]        = aA(A, 0.38f);
    c[ImGuiCol_DragDropTarget]        = ImVec4(0.95f, 0.70f, 0.30f, 0.90f);
    c[ImGuiCol_ResizeGrip]            = ImVec4(0.30f, 0.33f, 0.40f, 0.40f);
    c[ImGuiCol_ResizeGripHovered]     = aA(A, 0.65f);
    c[ImGuiCol_ResizeGripActive]      = aA(AH, 0.90f);
    c[ImGuiCol_NavCursor]             = A;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.05f, 0.05f, 0.06f, 0.55f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.04f, 0.04f, 0.06f, 0.60f);
}

} // namespace Engine
