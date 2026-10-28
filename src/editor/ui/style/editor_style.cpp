#include "ui/editor_theme.h"

#include <imgui.h>

#include "ui/editor_style.h"

namespace Vkm::Engine {

void applyEditorTheme(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 6.0f;
    style.WindowBorderSize  = 1.0f;
    style.WindowPadding     = ImVec2(10, 8);
    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    style.FrameRounding     = 5.0f;
    style.FramePadding      = ImVec2(8, 4);
    style.FrameBorderSize   = 0.0f;
    style.PopupRounding     = 6.0f;
    style.GrabRounding      = 4.0f;
    style.GrabMinSize       = 11.0f;
    style.ItemSpacing       = ImVec2(9, 7);
    style.ItemInnerSpacing  = ImVec2(6, 5);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 13.0f;
    style.ScrollbarRounding = 8.0f;
    // Square tabs sit flush on their panel; rounded ones read as floating buttons.
    style.TabRounding       = 0.0f;
    // Children tile edge-to-edge; rounded corners leave gaps at the seams.
    style.ChildRounding     = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.CellPadding       = ImVec2(7, 4);
    style.SeparatorTextBorderSize = 2.0f;
    style.SeparatorTextAlign      = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding    = ImVec2(16.0f, 4.0f);
    style.PopupBorderSize   = 1.0f;
    style.TabBarBorderSize  = 1.0f;
    // Selection gets a mark hover cannot imitate; a shade alone loses to the hover highlight.
    style.TabBarOverlineSize = 2.0f;
    // No window-menu arrow: panels are shown and hidden from the Window menu.
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.WindowMinSize     = ImVec2(220.0f, 140.0f);
    style.DisabledAlpha     = 0.45f;

    // The metrics above are reference-font pixels; scale them (borders too) with the font.
    style.ScaleAllSizes(scale);

    const ImVec4 accent      = EditorStyle::ACCENT;
    const ImVec4 accentHover = EditorStyle::ACCENT_HOV;
    auto aA = [](ImVec4 v, float a) {
        v.w = a;
        return v;
    };

    ImVec4* c = style.Colors;

    // Slots the base dark theme leaves with legacy values (orange plots, grey dimmed tabs).
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_TabSelectedOverline]   = accent;
    c[ImGuiCol_TabDimmedSelectedOverline] = aA(accent, 0.45f);
    c[ImGuiCol_TextLink]              = accentHover;
    c[ImGuiCol_PlotLinesHovered]      = accentHover;
    c[ImGuiCol_PlotHistogramHovered]  = accentHover;
    c[ImGuiCol_InputTextCursor]       = accent;
    c[ImGuiCol_TreeLines]             = ImVec4(0.32f, 0.35f, 0.44f, 0.35f);

    c[ImGuiCol_Text]                  = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.46f, 0.48f, 0.55f, 1.00f);
    c[ImGuiCol_WindowBg]              = ImVec4(0.105f, 0.110f, 0.125f, 1.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(0.125f, 0.130f, 0.150f, 1.00f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.145f, 0.155f, 0.180f, 0.98f);
    c[ImGuiCol_Border]                = ImVec4(0.32f, 0.35f, 0.44f, 0.45f);
    // A field is one step darker than the panel, not a black slab.
    c[ImGuiCol_FrameBg]               = ImVec4(0.088f, 0.093f, 0.112f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.175f, 0.195f, 0.235f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.225f, 0.250f, 0.305f, 1.00f);
    // A dock's tab bar uses the title colours: one step darker than the panel, the shade
    // of the gaps between panels, so the strip reads as frame and the panel below as page.
    // Focus shows as the selected tab's overline.
    c[ImGuiCol_TitleBg]               = c[ImGuiCol_WindowBg];
    c[ImGuiCol_TitleBgActive]         = c[ImGuiCol_WindowBg];
    c[ImGuiCol_TitleBgCollapsed]      = c[ImGuiCol_WindowBg];
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.130f, 0.140f, 0.165f, 1.00f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.070f, 0.075f, 0.090f, 0.50f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.33f, 0.40f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.40f, 0.44f, 0.52f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = aA(accent, 0.85f);
    c[ImGuiCol_CheckMark]             = accentHover;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accentHover;
    c[ImGuiCol_Button]                = ImVec4(0.185f, 0.200f, 0.240f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = aA(accent, 0.85f);
    c[ImGuiCol_ButtonActive]          = aA(accent, 1.00f);
    c[ImGuiCol_Header]                = ImVec4(0.205f, 0.235f, 0.300f, 1.00f);
    c[ImGuiCol_HeaderHovered]         = aA(accent, 0.55f);
    c[ImGuiCol_HeaderActive]          = aA(accent, 0.75f);
    c[ImGuiCol_Separator]             = ImVec4(0.26f, 0.29f, 0.36f, 0.55f);
    c[ImGuiCol_SeparatorHovered]      = aA(accent, 0.70f);
    // A tab is not a button: an unselected one is the strip itself, so only its label
    // shows, and the selected one is the panel's own shade, open into the page below it.
    c[ImGuiCol_Tab]                   = c[ImGuiCol_WindowBg];
    c[ImGuiCol_TabDimmed]             = c[ImGuiCol_Tab];
    // Hover is a neutral lift: an accent-coloured hover would outshine the selected tab.
    c[ImGuiCol_TabHovered]            = ImVec4(0.150f, 0.158f, 0.182f, 1.00f);
    c[ImGuiCol_TabSelected]           = c[ImGuiCol_ChildBg];
    c[ImGuiCol_TabDimmedSelected]     = c[ImGuiCol_TabSelected];
    c[ImGuiCol_PlotLines]             = accentHover;
    c[ImGuiCol_PlotHistogram]         = aA(accentHover, 0.85f);
    c[ImGuiCol_TableHeaderBg]         = ImVec4(0.150f, 0.160f, 0.190f, 1.00f);
    c[ImGuiCol_TableBorderStrong]     = ImVec4(0.26f, 0.29f, 0.36f, 1.00f);
    c[ImGuiCol_TableBorderLight]      = ImVec4(0.19f, 0.21f, 0.26f, 1.00f);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.025f);

    // The rest, so nothing falls back to StyleColorsDark's teal defaults.
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TextSelectedBg]        = aA(accent, 0.38f);
    c[ImGuiCol_DragDropTarget]        = ImVec4(0.95f, 0.70f, 0.30f, 0.90f);
    c[ImGuiCol_ResizeGrip]            = ImVec4(0.30f, 0.33f, 0.40f, 0.40f);
    c[ImGuiCol_ResizeGripHovered]     = aA(accent, 0.65f);
    c[ImGuiCol_ResizeGripActive]      = aA(accentHover, 0.90f);
    c[ImGuiCol_DockingPreview]        = aA(accent, 0.45f);
    c[ImGuiCol_DockingEmptyBg]        = c[ImGuiCol_WindowBg];
    c[ImGuiCol_NavCursor]             = accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.05f, 0.05f, 0.06f, 0.55f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.04f, 0.04f, 0.06f, 0.60f);
}

} // namespace Vkm::Engine
