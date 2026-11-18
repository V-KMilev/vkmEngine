// The editor's menus as a person opens them, through ImGui with no window: the Hierarchy's "+" is the
// Create menu itself, and Keyboard & Mouse lists every fixed control, each key under where it works.

#include <cstddef>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "chrome/editor_menu_bar.h"
#include "panels/hierarchy_panel.h"
#include "session/project/build_controller.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;

TEST("the Hierarchy's + opens the Create menu itself, a row of which makes an entity") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Pointer pointer;
    HierarchyPanel panel;
    const auto draw = [&] { panel.draw(desk.ec); };

    ImGuiID plus = 0;
    pointer.frame([&] {
        draw();
        plus = ImGui::GetID("###create");
    });
    ImGui::ActivateItemByID(plus);
    pointer.frame(draw);

    // The Empty row as the open popup holds it: under a lone "Create" submenu it would be a level down.
    ImGuiID empty = 0;
    pointer.frame([&] {
        draw();
        if (!ImGui::BeginPopup("##CreatePopup")) return;
        ImGui::PushID("Empty");
        empty = ImGui::GetID("##row");
        ImGui::PopID();
        ImGui::EndPopup();
    });
    REQUIRE(empty != 0);
    const size_t before = desk.scene().entityCount();
    ImGui::ActivateItemByID(empty);
    pointer.frame(draw);
    CHECK(desk.scene().entityCount() == before + 1);
}

TEST("Keyboard & Mouse lists the fixed controls, and Toggle Editor among the keys live in a session") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    desk.state.projectOpen = true;
    Pointer pointer;
    EditorMenuBar bar;
    BuildController build;

    // The bar in a window that has one, as the editor's dock space; whatever runs after it this frame.
    const auto frame = [&](const auto& after) {
        pointer.frame([&] {
            ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_MenuBar);
            bar.draw(desk.ec, build);
            if (ImGui::BeginMenuBar()) {
                after();
                ImGui::EndMenuBar();
            }
            ImGui::End();
        });
    };

    // Help, then its Keyboard & Mouse, as a click on each.
    ImGuiID help = 0;
    frame([&] { help = ImGui::GetID("Help"); });
    ImGui::ActivateItemByID(help);
    frame([] {});
    ImGuiID shortcuts = 0;
    frame([&] {
        if (!ImGui::BeginMenu("Help")) return;
        shortcuts = ImGui::GetID("Keyboard & Mouse");
        ImGui::EndMenu();
    });
    REQUIRE(shortcuts != 0);
    ImGui::ActivateItemByID(shortcuts);
    frame([] {});

    // The window's words, as ImGui's log takes them down.
    std::string shown;
    pointer.frame([&] {
        ImGui::LogToBuffer();
        ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_MenuBar);
        bar.draw(desk.ec, build);
        ImGui::End();
        shown = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
    });

    for (const char* row : {
             "Alt + drag a gizmo handle",
             "Double-click an asset",
             "Drop a file on the editor",
             "Revert to Prefab, Remove Component",
             "Ctrl + F in a panel",
             "F2 over the selected row or a loaded tile",
             "Arrows in the Hierarchy"
         }) {
        CHECK(shown.find(row) != std::string::npos);
    }
    const size_t play    = shown.find("Play (live during a session)");
    const size_t toggle  = shown.find("Toggle Editor");
    const size_t windows = shown.find("Windows & Panels");
    REQUIRE(play != std::string::npos);
    REQUIRE(windows != std::string::npos);
    CHECK((toggle > play && toggle < windows));
}
