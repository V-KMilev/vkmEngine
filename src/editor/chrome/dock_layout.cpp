#include "chrome/dock_layout.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/editor_style.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Open a dock node on @p window's tab.
 *
 * Without this a new node opens on the window docked into it last. The tab id
 * ("#TAB" in the window's id space) is unpublished ImGui; tests/editor/authoring_tests.cpp
 * fails when it moves.
 *
 * @param node   Node already holding the window.
 * @param window Window to open on.
 */
void openOnTab(ImGuiID node, const char* window) {
    ImGui::DockBuilderGetNode(node)->SelectedTabId = ImHashStr("#TAB", 0, ImHashStr(window));
}

} // namespace

bool hasLayout(unsigned int dockspace) {
    return ImGui::DockBuilderGetNode(dockspace) != nullptr;
}

void buildDefaultLayout(unsigned int dockspace, float width, float height) {
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImVec2(width, height));

    // Sides are cut first so they run full height. The remainder of each split
    // keeps the central node, so the viewport's is what is left after the third cut.
    ImGuiID rest   = 0;
    ImGuiID centre = 0;
    const ImGuiID left   = ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 0.2f, nullptr, &rest);
    const ImGuiID right  = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.25f, nullptr, &rest);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Down, 0.25f, nullptr, &centre);

    // Sizes, not shares: the first frame can come before the window is sized.
    const float bottomWidth = width - EditorStyle::px(HIERARCHY_WIDTH + INSPECTOR_WIDTH);
    ImGui::DockBuilderSetNodeSize(bottom, ImVec2(bottomWidth, EditorStyle::px(BOTTOM_HEIGHT)));
    ImGui::DockBuilderSetNodeSize(left,   ImVec2(EditorStyle::px(HIERARCHY_WIDTH), height));
    ImGui::DockBuilderSetNodeSize(right,  ImVec2(EditorStyle::px(INSPECTOR_WIDTH), height));

    ImGui::DockBuilderDockWindow(HIERARCHY_WINDOW, left);
    ImGui::DockBuilderDockWindow(VIEWPORT_WINDOW,  centre);
    ImGui::DockBuilderDockWindow(INSPECTOR_WINDOW, right);
    ImGui::DockBuilderDockWindow(MATERIAL_WINDOW,  right);
    ImGui::DockBuilderDockWindow(ASSETS_WINDOW,    bottom);
    ImGui::DockBuilderDockWindow(ANIMATION_WINDOW, bottom);
    ImGui::DockBuilderDockWindow(ERRORS_WINDOW,    bottom);
    ImGui::DockBuilderDockWindow(BUILD_WINDOW,     bottom);
    openOnTab(right,  INSPECTOR_WINDOW);
    openOnTab(bottom, ASSETS_WINDOW);
    ImGui::DockBuilderFinish(dockspace);
}

void setViewportWindowClass() {
    ImGuiWindowClass viewport;
    viewport.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
    ImGui::SetNextWindowClass(&viewport);
}

void dockNextBeside(const char* beside) {
    const ImGuiWindow* neighbour = ImGui::FindWindowByName(beside);
    if (neighbour && neighbour->DockId) ImGui::SetNextWindowDockID(neighbour->DockId, ImGuiCond_FirstUseEver);
}

} // namespace Vkm::Engine
