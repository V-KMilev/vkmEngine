// The workspace's first layout, as a first launch and Window > Reset Layout build it, through ImGui with
// no window.

#include <set>
#include <string>

#include <imgui.h>

#include "chrome/dock_layout.h"
#include "ui/window_names.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;

TEST("the first layout, and the layout reset, open the right side on the Inspector and the bottom on the Assets") {
    Pointer pointer;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    const char* const panels[] = {
        HIERARCHY_WINDOW,
        TERRAIN_WINDOW,
        INSPECTOR_WINDOW,
        CONSOLE_WINDOW,
        ANIMATION_WINDOW,
        ASSETS_WINDOW,
        VIEWPORT_WINDOW,
    };

    // As the editor's frame goes: the layout before the dockspace, then each panel, taking no focus but
    // the one asked to come forward. A reset is asked from the menu bar, which takes the focus. The
    // Inspector and the Assets are begun last of their sides: the layout picks the tab, not the order.
    std::set<std::string> showing;
    const auto frame = [&](bool reset, const char* forward) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        if (reset) ImGui::SetNextWindowFocus();
        ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoDecoration);
        const ImGuiID dockspace = ImGui::GetID("##Workspace");
        if (reset || !hasLayout(dockspace)) buildDefaultLayout(dockspace, Pointer::WIDTH, Pointer::HEIGHT);
        ImGui::DockSpace(dockspace, ImVec2(Pointer::WIDTH, Pointer::HEIGHT));
        ImGui::End();
        showing.clear();
        for (const char* panel : panels) {
            if (forward && std::string(panel) == forward) ImGui::SetNextWindowFocus();
            if (ImGui::Begin(panel, nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) showing.insert(panel);
            ImGui::End();
        }
        ImGui::Render();
    };
    const auto opensOnInspectorAndAssets = [&] {
        return showing.count(INSPECTOR_WINDOW) == 1 && showing.count(TERRAIN_WINDOW) == 0
            && showing.count(ASSETS_WINDOW) == 1 && showing.count(ANIMATION_WINDOW) == 0
            && showing.count(CONSOLE_WINDOW) == 0;
    };

    for (int i = 0; i < 3; ++i) frame(false, nullptr);
    CHECK(opensOnInspectorAndAssets());

    // The Terrain and the Console brought forward, then Window > Reset Layout.
    frame(false, TERRAIN_WINDOW);
    frame(false, CONSOLE_WINDOW);
    frame(false, nullptr);
    CHECK(showing.count(TERRAIN_WINDOW) == 1);
    CHECK(showing.count(CONSOLE_WINDOW) == 1);
    frame(true, nullptr);
    for (int i = 0; i < 2; ++i) frame(false, nullptr);
    CHECK(opensOnInspectorAndAssets());
}
