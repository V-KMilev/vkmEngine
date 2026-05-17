#include "ui/editor_theme.h"

#include <imgui.h>

namespace Engine {

void applyEditorTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.WindowPadding     = ImVec2(8, 6);
    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    style.FrameRounding     = 3.0f;
    style.FramePadding      = ImVec2(6, 4);
    style.FrameBorderSize   = 0.0f;
    style.GrabRounding      = 2.0f;
    style.GrabMinSize       = 10.0f;
    style.ItemSpacing       = ImVec2(6, 5);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 14.0f;
    style.ScrollbarSize     = 12.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding       = 4.0f;
    style.PopupRounding     = 4.0f;
    // Docked panels are child regions tiled edge-to-edge; rounded corners
    // leave triangular gaps at the seams. Square them (floating windows
    // keep WindowRounding).
    style.ChildRounding     = 0.0f;
    style.ChildBorderSize   = 0.5f;
    style.CellPadding       = ImVec2(4, 3);
    style.SeparatorTextBorderSize = 2.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = ImVec4(0.88f, 0.89f, 0.90f, 1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.51f, 0.54f, 1.00f);
    c[ImGuiCol_WindowBg]              = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.15f, 0.15f, 0.16f, 0.98f);
    c[ImGuiCol_Border]                = ImVec4(0.20f, 0.20f, 0.22f, 0.50f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.18f, 0.18f, 0.21f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    c[ImGuiCol_TitleBg]               = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.08f, 0.08f, 0.09f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.40f, 0.40f, 0.44f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.50f, 0.50f, 0.54f, 1.00f);
    c[ImGuiCol_CheckMark]             = ImVec4(0.45f, 0.72f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]            = ImVec4(0.36f, 0.60f, 0.92f, 1.00f);
    c[ImGuiCol_SliderGrabActive]      = ImVec4(0.46f, 0.70f, 1.00f, 1.00f);
    c[ImGuiCol_Button]                = ImVec4(0.18f, 0.18f, 0.21f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.30f, 0.50f, 0.78f, 1.00f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.25f, 0.44f, 0.70f, 1.00f);
    c[ImGuiCol_Header]                = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.26f, 0.44f, 0.70f, 0.50f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.26f, 0.44f, 0.70f, 0.70f);
    c[ImGuiCol_Separator]             = ImVec4(0.22f, 0.22f, 0.24f, 0.40f);
    c[ImGuiCol_SeparatorHovered]      = ImVec4(0.36f, 0.60f, 0.92f, 0.60f);
    c[ImGuiCol_Tab]                   = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    c[ImGuiCol_TabHovered]            = ImVec4(0.30f, 0.50f, 0.78f, 0.75f);
    c[ImGuiCol_TabSelected]           = ImVec4(0.22f, 0.36f, 0.56f, 1.00f);
    c[ImGuiCol_PlotLines]             = ImVec4(0.45f, 0.72f, 1.00f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = ImVec4(0.45f, 0.72f, 1.00f, 0.80f);
    c[ImGuiCol_TableHeaderBg]         = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    c[ImGuiCol_TableBorderStrong]     = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_TableBorderLight]      = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);

    // Fill the remaining slots so nothing falls back to StyleColorsDark's
    // teal defaults (these are what made stray bits look "un-themed").
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TextSelectedBg]        = ImVec4(0.30f, 0.50f, 0.78f, 0.40f);
    c[ImGuiCol_DragDropTarget]        = ImVec4(0.95f, 0.70f, 0.30f, 0.90f);
    c[ImGuiCol_ResizeGrip]            = ImVec4(0.30f, 0.30f, 0.34f, 0.40f);
    c[ImGuiCol_ResizeGripHovered]     = ImVec4(0.36f, 0.60f, 0.92f, 0.65f);
    c[ImGuiCol_ResizeGripActive]      = ImVec4(0.46f, 0.70f, 1.00f, 0.90f);
    c[ImGuiCol_NavHighlight]          = ImVec4(0.36f, 0.60f, 0.92f, 1.00f);
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.05f, 0.05f, 0.06f, 0.55f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.05f, 0.05f, 0.06f, 0.55f);
}

} // namespace Engine
