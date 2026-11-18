#include "ui/style/editor_style.h"

#include <imgui.h>


namespace Vkm::Engine {

namespace {
ImFont* g_headingFont = nullptr;
} // namespace

ImFont* EditorStyle::headingFont() { return g_headingFont; }

void EditorStyle::setHeadingFont(ImFont* font) { g_headingFont = font; }

void applyEditorTheme(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 6.0f;
    style.WindowBorderSize  = 1.0f;
    style.WindowPadding     = ImVec2(10, 8);
    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    style.FrameRounding     = 5.0f;
    // A 20 px field and 4 px between rows: 24 px a row at the 14 px font.
    style.FramePadding      = ImVec2(6, 3);
    style.FrameBorderSize   = 0.0f;
    style.PopupRounding     = 6.0f;
    style.GrabRounding      = 4.0f;
    style.GrabMinSize       = 10.0f;
    style.ItemSpacing       = ImVec2(8, 4);
    style.ItemInnerSpacing  = ImVec2(6, 4);
    style.IndentSpacing     = 16.0f;
    style.ScrollbarSize     = 10.0f;
    style.ScrollbarRounding = 5.0f;
    // A hairline between docked panels; the gap's shade does the separating.
    style.DockingSeparatorSize = 1.0f;
    // Square tabs sit flush on their panel; rounded ones read as floating buttons.
    style.TabRounding = 0.0f;
    // Children tile edge-to-edge; rounded corners leave gaps at the seams.
    style.ChildRounding           = 0.0f;
    style.ChildBorderSize         = 1.0f;
    style.CellPadding             = ImVec2(6, 3);
    style.SeparatorTextBorderSize = 2.0f;
    style.SeparatorTextAlign      = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding    = ImVec2(16.0f, 4.0f);
    style.PopupBorderSize         = 1.0f;
    style.TabBarBorderSize        = 1.0f;
    // Selection gets a mark hover cannot imitate; a shade alone loses to the hover highlight.
    style.TabBarOverlineSize = 2.0f;
    // No window-menu arrow: panels are shown and hidden from the Window menu.
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.WindowMinSize            = ImVec2(220.0f, 140.0f);
    style.DisabledAlpha            = 0.45f;

    // The metrics above are reference-font pixels; scale them (borders too) with the font.
    style.ScaleAllSizes(scale);

    using namespace EditorStyle;
    auto aA = [](ImVec4 v, float a) {
        v.w = a;
        return v;
    };

    ImVec4* c = style.Colors;

    c[ImGuiCol_Text]                  = TEXT;
    c[ImGuiCol_TextDisabled]          = TEXT_SECONDARY;
    c[ImGuiCol_TextLink]              = ACCENT_HOV;
    c[ImGuiCol_TextSelectedBg]        = aA(ACCENT, 0.30f);
    c[ImGuiCol_InputTextCursor]       = ACCENT;

    // The root window shows only in the gaps between panels; a panel is a step lighter.
    c[ImGuiCol_WindowBg]              = GAP;
    c[ImGuiCol_ChildBg]               = PANEL;
    c[ImGuiCol_PopupBg]               = RAISED;
    c[ImGuiCol_MenuBarBg]             = GAP;
    c[ImGuiCol_Border]                = HAIRLINE;
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_Separator]             = HAIRLINE;
    c[ImGuiCol_SeparatorHovered]      = aA(ACCENT, 0.70f);
    c[ImGuiCol_SeparatorActive]       = ACCENT;
    c[ImGuiCol_TreeLines]             = HAIRLINE;

    // A field is a step darker than the panel; under the pointer it rises as a button does.
    c[ImGuiCol_FrameBg]               = FIELD;
    c[ImGuiCol_FrameBgHovered]        = HOVERED;
    c[ImGuiCol_FrameBgActive]         = PRESSED;
    c[ImGuiCol_Button]                = RAISED;
    c[ImGuiCol_ButtonHovered]         = HOVERED;
    c[ImGuiCol_ButtonActive]          = PRESSED;
    // A selected row is the accent; hovering one is a neutral lift.
    c[ImGuiCol_Header]                = aA(ACCENT, 0.30f);
    c[ImGuiCol_HeaderHovered]         = HOVERED;
    c[ImGuiCol_HeaderActive]          = PRESSED;
    c[ImGuiCol_CheckMark]             = ACCENT;
    c[ImGuiCol_SliderGrab]            = ACCENT;
    c[ImGuiCol_SliderGrabActive]      = ACCENT_HOV;

    // A dock's tab bar is the gaps' shade, so the strip reads as frame and the panel below as page.
    // An unselected tab is the strip itself; the selected one is the panel's shade, open into it.
    // Focus shows as the selected tab's overline.
    c[ImGuiCol_TitleBg]                   = GAP;
    c[ImGuiCol_TitleBgActive]             = GAP;
    c[ImGuiCol_TitleBgCollapsed]          = GAP;
    c[ImGuiCol_Tab]                       = GAP;
    c[ImGuiCol_TabDimmed]                 = GAP;
    c[ImGuiCol_TabHovered]                = HOVERED;
    c[ImGuiCol_TabSelected]               = PANEL;
    c[ImGuiCol_TabDimmedSelected]         = PANEL;
    c[ImGuiCol_TabSelectedOverline]       = ACCENT;
    c[ImGuiCol_TabDimmedSelectedOverline] = aA(ACCENT, 0.45f);

    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ScrollbarGrab]         = HOVERED;
    c[ImGuiCol_ScrollbarGrabHovered]  = PRESSED;
    c[ImGuiCol_ScrollbarGrabActive]   = TEXT_SECONDARY;
    c[ImGuiCol_ResizeGrip]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ResizeGripHovered]     = aA(ACCENT, 0.65f);
    c[ImGuiCol_ResizeGripActive]      = ACCENT;

    c[ImGuiCol_PlotLines]             = ACCENT_HOV;
    c[ImGuiCol_PlotLinesHovered]      = ACCENT_HOV;
    c[ImGuiCol_PlotHistogram]         = aA(ACCENT_HOV, 0.85f);
    c[ImGuiCol_PlotHistogramHovered]  = ACCENT_HOV;
    c[ImGuiCol_TableHeaderBg]         = RAISED;
    c[ImGuiCol_TableBorderStrong]     = HAIRLINE;
    c[ImGuiCol_TableBorderLight]      = HAIRLINE;
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.025f);

    c[ImGuiCol_DragDropTarget]        = ACCENT;
    c[ImGuiCol_DockingPreview]        = aA(ACCENT, 0.45f);
    c[ImGuiCol_DockingEmptyBg]        = GAP;
    c[ImGuiCol_NavCursor]             = ACCENT;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.05f, 0.05f, 0.06f, 0.55f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.04f, 0.04f, 0.06f, 0.60f);
}

} // namespace Vkm::Engine
