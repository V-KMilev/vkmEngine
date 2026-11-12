// Where the editor's settings live: the project keeps what it shows and each scene's view, the person
// keeps how they like the editor, and both come back as they went.

#include <filesystem>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include "core/host_chrome.h"
#include "editor_settings.h"
#include "io/json/reflect_json.h"
#include "io/project/project.h"
#include "panels/preferences_panel.h"
#include "panels/project_settings_panel.h"
#include "ui/window_names.h"
#include "vkm_platform/input/input_handle.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;
using namespace Vkm::Platform;

TEST("the project's editor settings come back as they went, and only for scenes still on disk") {
    const std::filesystem::path root = openProject();
    const std::string level = (root / "scenes" / "level.json").string();
    const std::string gone  = (root / "scenes" / "gone.json").string();
    writeText(level, "{}");

    EditorState saved;
    HostChrome  shown;
    saved.workspace.showTerrain   = true;
    saved.workspace.showHierarchy = false;
    shown.display.mode            = RenderMode::Wireframe;
    shown.display.gridAxisX       = false;
    shown.display.gridAxisY       = true;
    shown.display.gridAxisZ       = true;

    EditorViewpoint view;
    view.position = {1.0f, 2.0f, 3.0f};
    view.yaw      = 0.5f;

    saved.sceneViews["scenes/level.json"] = view;
    saved.sceneViews["scenes/gone.json"]  = view;
    saved.recentScenes                    = {level, gone};
    REQUIRE(EditorSettings::save(saved, shown));

    EditorState read;
    HostChrome  readShown;
    REQUIRE(EditorSettings::load(read, readShown));
    CHECK(ReflectJson::toJson(read.workspace) == ReflectJson::toJson(saved.workspace));
    CHECK(readShown.display.mode == RenderMode::Wireframe);
    CHECK(!readShown.display.gridAxisX);
    CHECK(readShown.display.gridAxisY);
    CHECK(readShown.display.gridAxisZ);
    CHECK(read.recentScenes == std::vector<std::string>{level});
    REQUIRE(read.sceneViews.count("scenes/level.json") == 1);
    CHECK(read.sceneViews.count("scenes/gone.json") == 0);
    CHECK(read.sceneViews["scenes/level.json"].position == view.position);
    CHECK(read.sceneViews["scenes/level.json"].yaw == view.yaw);
}

TEST("a person's preferences follow them across projects, and a file of another version is read as the defaults") {
    Pointer imgui;  // the keys are written by ImGui's names
    openProject("first");
    EditorState saved;
    saved.prefs.camera.moveSpeed   = 25.0f;
    saved.prefs.snapEnabled        = true;
    saved.prefs.keybinds.playStop  = KeyBind{ImGuiKey_F7, KEY_MOD_SHIFT};
    saved.recentProjects           = {"/somewhere/else"};
    REQUIRE(EditorSettings::saveUser(saved));

    openProject("second");
    EditorState read;
    EditorSettings::loadUser(read);
    CHECK(read.prefs.camera.moveSpeed == 25.0f);
    CHECK(read.prefs.snapEnabled);
    CHECK(read.prefs.keybinds.playStop == KeyBind{ImGuiKey_F7, KEY_MOD_SHIFT});
    CHECK(read.recentProjects == std::vector<std::string>{"/somewhere/else"});

    // Another version's file would mean something else: none of it is taken, and the log says why.
    const std::filesystem::path project = EditorSettings::pathIn(openProject("third"));
    writeText(project, "{ \"version\": 1, \"workspace\": { \"showTerrain\": true } }");
    EditorState fresh;
    HostChrome  freshShown;
    CHECK(!EditorSettings::load(fresh, freshShown));
    CHECK(!fresh.workspace.showTerrain);
    CHECK(countInLog("Editor settings is version 1") == 1);
}

TEST("Project Settings binds an action to the next key or button pressed, and Save writes it by name") {
    Bench bench;
    Desk& desk = bench.desk;
    const std::filesystem::path root = openProject();
    desk.state.project.actions     = {{"Jump", {InputBinding::key(Key::Space)}}};
    desk.state.projectSaved        = desk.state.project;
    desk.state.showProjectSettings = true;

    Pointer pointer;
    ProjectSettingsPanel panel;
    InputHandle& hand = bench.frame.window.getInputHandle();

    const auto draw = [&] { panel.draw(desk.ec); };

    // The window's own ids for the action's "+" and for Save, with the Input card opened as a click
    // on its header leaves it.
    ImGuiID plus = 0;
    ImGuiID save = 0;
    pointer.frame([&] {
        draw();
        ImGui::Begin("Project Settings");
        ImGui::PushID("Input");
        ImGui::GetStateStorage()->SetBool(ImGui::GetID("##open"), true);
        ImGui::PushID(0);
        plus = ImGui::GetID("+");
        ImGui::PopID();
        ImGui::PopID();
        save = ImGui::GetID("Save project.json");
        ImGui::End();
    });

    // "+", then the next key down; then "+", then a mouse button.
    ImGui::ActivateItemByID(plus);
    pointer.frame(draw);
    hand.onKeyEvent(Key::KeypadAdd, true);
    pointer.frame(draw);
    hand.onKeyEvent(Key::KeypadAdd, false);
    ImGui::ActivateItemByID(plus);
    pointer.frame(draw);
    hand.setButton(MouseButton::Button4, true);
    pointer.frame(draw);
    hand.setButton(MouseButton::Button4, false);

    const std::vector<InputBinding>& bound = desk.state.project.actions[0].bindings;
    REQUIRE(bound.size() == 3);
    CHECK(bound[1].is(Key::KeypadAdd));
    CHECK(bound[2].is(MouseButton::Button4));

    ImGui::ActivateItemByID(save);
    pointer.frame(draw);
    const nlohmann::json file = nlohmann::json::parse(readText(root / "project.json"));
    CHECK(file["actions"]["Jump"] == nlohmann::json::parse(R"(["Space", "Keypad +", "Mouse 4"])"));
    CHECK(desk.ec.frame.input.bindings("Jump").size() == 3);
    Project read;
    REQUIRE(loadProject(root, read));
    REQUIRE(read.actions.size() == 1);
    REQUIRE(read.actions[0].bindings.size() == 3);
    CHECK(read.actions[0].bindings[1].is(Key::KeypadAdd));
    CHECK(read.actions[0].bindings[2].is(MouseButton::Button4));
}

TEST("Project Settings says a field it shows is unsaved until Save writes it, the start screen's picture too") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    desk.state.projectSaved        = desk.state.project;
    desk.state.showProjectSettings = true;
    Pointer pointer;
    ProjectSettingsPanel panel;

    // The window's words, as ImGui's log takes them down.
    const auto shown = [&] {
        std::string words;
        pointer.frame([&] {
            ImGui::LogToBuffer();
            panel.draw(desk.ec);
            words = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
        });
        return words;
    };
    CHECK(shown().find("Unsaved") == std::string::npos);
    desk.state.project.picture = "picture.png";
    CHECK(shown().find("Unsaved") != std::string::npos);
}

TEST("the frame cap row keeps to its range, a step past the top too") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    desk.state.prefs.fpsCap    = 990;
    desk.state.showPreferences = true;
    Pointer pointer;
    PreferencesPanel panel;
    const auto draw = [&] { panel.draw(desk.ec); };

    // The Display tab, as a click on it; then its frame cap's "+".
    pointer.frame([&] {
        draw();
        ImGui::Begin(PREFERENCES_WINDOW);
        ImGuiTabBar* tabs = ImGui::GetCurrentContext()->TabBars.GetByKey(ImGui::GetID("##PrefTabs"));
        REQUIRE(tabs);
        // A tab is named inside its bar's id scope.
        ImGui::PushOverrideID(tabs->ID);
        ImGui::TabBarQueueFocus(tabs, "Display");
        ImGui::PopID();
        ImGui::End();
    });
    ImGuiID plus = 0;
    pointer.frame([&] {
        draw();
        ImGui::Begin(PREFERENCES_WINDOW);
        ImGuiTabBar* tabs = ImGui::GetCurrentContext()->TabBars.GetByKey(ImGui::GetID("##PrefTabs"));
        ImGui::PushOverrideID(tabs->SelectedTabId);
        ImGui::PushID("fpsCap");
        ImGui::PushID("##v");
        plus = ImGui::GetID("+");
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::End();
    });
    ImGui::ActivateItemByID(plus);
    pointer.frame(draw);
    CHECK(desk.state.prefs.fpsCap == 1000);
}
