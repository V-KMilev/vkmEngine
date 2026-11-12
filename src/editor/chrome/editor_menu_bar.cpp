#include "chrome/editor_menu_bar.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#include <imgui.h>

#include "actions/create_menu.h"
#include "chrome/dock_layout.h"
#include "core/frame_context.h"
#include "core/clock.h"
#include "ecs/scene.h"
#include "ecs/component/scene/transform.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "editor_context.h"
#include "session/project/build_controller.h"
#include "session/play_session.h"
#include "session/scene_session.h"
#include "actions/edit_actions.h"
#include "actions/entity_clipboard.h"
#include "actions/prefab_actions.h"
#include "view/view_framing.h"
#include "io/project/roots.h"
#include "system/render/render_backend.h"
#include "system/render/render_system.h"
#include "ui/editor_dialogs.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"
#include "build_stamp.h"

namespace Vkm::Engine {

namespace {

// One label and value row of the About dialog. valueX is from the row's start, not the
// window's, so the rows inside the Debug tree align against its indent.
void aboutRow(float valueX, const char* label, const char* value) {
    const float startX = ImGui::GetCursorPosX();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(startX + valueX);
    ImGui::TextUnformatted(value);
}
// "Undo Transform": the verb and the top step's label, or the verb alone at the stack's end (null op).
void historyItemLabel(char* buf, size_t n, const char* verb, const char* op) {
    snprintf(buf, n, "%s%s%s", verb, op ? " " : "", op ? op : "");
}
} // namespace

void EditorMenuBar::drawSelectionItems(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    const bool haveSel = ctx.scene.isAlive(state.selection.active());
    const auto& kb     = state.prefs.keybinds;
    if (ImGui::MenuItem("Cut", keyLabel(kb.cutEntity), false, haveSel)) {
        EditorActions::cutSelection(ctx.scene, ctx.resources, state);
    }
    if (ImGui::MenuItem("Copy", keyLabel(kb.copyEntity), false, haveSel)) {
        EditorActions::copySelection(ctx.scene, ctx.resources, state);
    }
    if (ImGui::MenuItem("Paste", keyLabel(kb.pasteEntity), false, state.entityClipboard != nullptr)) {
        EditorActions::paste(ctx.scene, ctx.resources, state);
    }
    if (ImGui::MenuItem("Duplicate", keyLabel(kb.duplicate), false, haveSel)) {
        EditorActions::duplicateSelection(ctx.scene, ctx.resources, state);
    }
    if (ImGui::MenuItem("Delete", keyLabel(state.prefs.keybinds.deleteEntity), false, haveSel)) {
        EditorActions::deleteSelection(ctx.scene, state);
    }
    if (ImGui::MenuItem("Select All", keyLabel(kb.selectAll), false, ctx.scene.entityCount() > 0)) {
        EditorActions::selectAll(ctx.scene, state);
    }
    if (ImGui::MenuItem("Deselect", keyLabel(kb.deselect), false, haveSel)) {
        state.selection.clear();
    }
}

void EditorMenuBar::drawPlacingItems(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    const bool haveSel = ctx.scene.isAlive(state.selection.active());
    const bool canMove = haveSel && ctx.scene.has<Transform>(state.selection.active());
    if (ImGui::MenuItem("Align With View", keyLabel(state.prefs.keybinds.alignWithView), false, canMove)) {
        EditorActions::alignWithView(ctx.scene, ctx.resources, state);
    }
    const bool canLook = canMove && ec.cameraController.isActive();
    if (ImGui::MenuItem("Align View to Selected", nullptr, false, canLook)) {
        ViewFraming::viewFromEntity(ctx.scene, state.selection.active(), ec.cameraController);
    }
    if (ImGui::MenuItem("Drop to Ground", keyLabel(state.prefs.keybinds.dropToGround), false, canMove)) {
        EditorActions::dropToGround(ctx.scene, ctx.resources, state);
    }
    if (ImGui::MenuItem("Group", keyLabel(state.prefs.keybinds.groupSelection), false, haveSel)) {
        EditorActions::groupSelection(ctx.scene, state);
    }
}

void EditorMenuBar::drawFileMenu(EditorContext& ec) {
    SceneSession& session = ec.session;
    FrameContext& ctx     = ec.frame;
    EditorState& state    = ec.state;

    if (ImGui::BeginMenu("File")) {
        const bool haveCurrent = session.hasPath();

        // The heading carries the noun, so rows drop it; rows that then read alike take a ## suffix,
        // as ImGui hashes an id from the label and two "Open..." would collide.
        ImGui::SeparatorText("Project");
        if (ImGui::MenuItem("New...")) state.requests.ask(NewProject{});
        if (ImGui::MenuItem("Open...##project")) state.requests.ask(Ask::OpenProject);
        if (beginSubmenu("Open Recent##project", !state.recentProjects.empty())) {
            for (const std::string& p : state.recentProjects) {
                ImGui::PushID(p.c_str());
                const std::string shortName = std::filesystem::path(p).filename().string();
                if (ImGui::MenuItem(shortName.empty() ? p.c_str() : shortName.c_str())) {
                    state.sceneActions.request(SceneActions::Kind::OpenProject, p);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Settings...##project")) state.showProjectSettings = true;
        ImGui::SeparatorText("Scene");
        if (ImGui::MenuItem("New", keyLabel(state.prefs.keybinds.newScene))) {
            state.sceneActions.request(SceneActions::Kind::New);
        }
        if (ImGui::MenuItem("Open...##scene", keyLabel(state.prefs.keybinds.loadScene))) {
            session.requestLoad();
        }

        const bool haveRecents = !state.recentScenes.empty();
        if (beginSubmenu("Open Recent##scene", haveRecents)) {
            for (const auto& p : state.recentScenes) {
                const std::string shortName = std::filesystem::path(p).filename().string();
                ImGui::PushID(p.c_str());
                if (ImGui::MenuItem(shortName.c_str())) {
                    state.sceneActions.request(SceneActions::Kind::Open, p);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear List")) state.recentScenes.clear();
            ImGui::EndMenu();
        }
        // Grayed in a play session: the scene is then the simulation's copy, and writing it over the
        // authored file is the one save that cannot be taken back.
        const bool playing = ec.play.isPlaying();
        const bool canSave = haveCurrent && !playing;
        if (ImGui::MenuItem("Save", keyLabel(state.prefs.keybinds.saveScene), false, canSave)) {
            session.save(ctx, state);
        }
        if (ImGui::MenuItem("Save As...", keyLabel(state.prefs.keybinds.saveSceneAs), false, !playing)) {
            session.requestSaveAs(state);
        }
        if (playing) ImGui::TextDisabled("Stop the play session to save");
        ImGui::Separator();
        // Run by vkm, in the Console's Build view; a build that changes the module reloads it.
        const bool canBuild = state.projectOpen && !BuildController::launcher().empty();
        const std::string root = Roots::projectRoot().string();
        const char* why = state.projectOpen ? "This engine has no vkm beside it" : "Open a project first";
        if (ImGui::MenuItem("Build Scripts", keyLabel(state.prefs.keybinds.buildScripts), false, canBuild)) {
            state.requests.ask(VkmRun{{"build", root}, {}});
        }
        if (!canBuild) ImGui::SetItemTooltip("%s", why);
        if (ImGui::MenuItem("Package Game...", nullptr, false, canBuild)) {
            state.requests.ask(VkmRun{{"package", root}, {}});
        }
        if (!canBuild) ImGui::SetItemTooltip("%s", why);
        if (ImGui::MenuItem("Reload Scripts", nullptr, false, state.projectOpen)) {
            state.requests.ask(Ask::ReloadScripts);
        }
        if (!state.projectOpen) ImGui::SetItemTooltip("Open a project first");

        if (haveCurrent) {
            ImGui::Separator();
            const std::string fname = std::filesystem::path(session.path()).filename().string();
            ImGui::TextDisabled("%s%s", fname.c_str(), state.sceneDirty ? "  (modified)" : "");
        }
        ImGui::Separator();
        // Requested, not the window's close flag: the frame loop reads that before the next frame,
        // so the editor would be gone before the close-intercept in the same stage could ask.
        if (ImGui::MenuItem("Exit")) state.sceneActions.request(SceneActions::Kind::Quit);
        ImGui::EndMenu();
    }
}

void EditorMenuBar::drawHistory(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    EditorState& state = ec.state;
    const CommandStack& commands = state.commands;

    // Oldest first, the step the scene now stands after ticked, what redo would bring dimmed.
    // A click walks undo or redo to that step.
    const size_t done    = commands.undoDepth();
    const size_t undone  = commands.redoDepth();
    const size_t shownUp = std::min(done, HISTORY_SHOWN);
    if (done > shownUp) ImGui::TextDisabled("%zu older", done - shownUp);
    else if (ImGui::MenuItem("(as opened)", nullptr, done == 0)) {
        for (size_t i = 0; i < done; ++i) EditorActions::undo(ctx.scene, state);
    }
    for (size_t back = shownUp; back-- > 0;) {
        ImGui::PushID(static_cast<int>(back));
        if (ImGui::MenuItem(commands.undoLabel(back), nullptr, back == 0)) {
            for (size_t i = 0; i < back; ++i) EditorActions::undo(ctx.scene, state);
        }
        ImGui::PopID();
    }
    const size_t shownDown = std::min(undone, HISTORY_SHOWN);
    for (size_t ahead = 0; ahead < shownDown; ++ahead) {
        ImGui::PushID(static_cast<int>(HISTORY_SHOWN + ahead));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const bool picked = ImGui::MenuItem(commands.redoLabel(ahead));
        ImGui::PopStyleColor();
        if (picked) {
            for (size_t i = 0; i <= ahead; ++i) EditorActions::redo(ctx.scene, state);
        }
        ImGui::PopID();
    }
    if (undone > shownDown) ImGui::TextDisabled("%zu more", undone - shownDown);
}

void EditorMenuBar::drawEditMenu(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (ImGui::BeginMenu("Edit")) {
        char undoText[80], redoText[80];
        historyItemLabel(undoText, sizeof(undoText), "Undo", state.commands.undoLabel());
        historyItemLabel(redoText, sizeof(redoText), "Redo", state.commands.redoLabel());
        if (ImGui::MenuItem(undoText, keyLabel(state.prefs.keybinds.undo), false, state.commands.canUndo())) {
            EditorActions::undo(ctx.scene, state);
        }
        if (ImGui::MenuItem(redoText, keyLabel(state.prefs.keybinds.redo), false, state.commands.canRedo())) {
            EditorActions::redo(ctx.scene, state);
        }
        const bool anyHistory = state.commands.canUndo() || state.commands.canRedo();
        if (beginSubmenu("History", anyHistory)) {
            drawHistory(ec);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        drawSelectionItems(ec);
        ImGui::Separator();
        // Shown and focused, not toggled: a floating Preferences can already be open behind
        // another window, where setting the flag alone does nothing.
        if (ImGui::MenuItem("Preferences...", keyLabel(state.prefs.keybinds.openPreferences))) {
            state.showPreferences = true;
            ImGui::SetWindowFocus(PREFERENCES_WINDOW);
        }
        ImGui::EndMenu();
    }
}

void EditorMenuBar::drawViewMenu(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (ImGui::BeginMenu("View")) {
        // The camera refuses to move while it stands down for a session, so
        // both read as unavailable then.
        const bool canFrame         = ec.cameraController.isActive();
        const bool canFrameSelected = canFrame && !!state.selection.active();
        const KeyLabel frameSelectedKey = keyLabel(state.prefs.keybinds.focusSelected);
        if (ImGui::MenuItem("Frame Selected", frameSelectedKey, false, canFrameSelected)) {
            ViewFraming::frameSelected(ctx, state.selection.entities(), ec.cameraController);
        }
        if (ImGui::MenuItem("Frame All", keyLabel(state.prefs.keybinds.frameAll), false, canFrame)) {
            ViewFraming::frameAll(ctx, ec.cameraController);
        }
        const KeyLabel orthoKey = keyLabel(state.prefs.keybinds.toggleOrthographic);
        if (ImGui::MenuItem("Orthographic", orthoKey, ec.cameraController.isOrthographic(), canFrame)) {
            ec.cameraController.setOrthographic(!ec.cameraController.isOrthographic());
        }
        ImGui::Separator();
        if (beginSubmenu("Show")) {
            HostChrome::Display grid = ec.frame.chrome.display;
            if (beginSubmenu("Grid")) {
                bool changed = ImGui::MenuItem("X Axis", nullptr, &grid.gridAxisX);
                changed |= ImGui::MenuItem("Y Axis", nullptr, &grid.gridAxisY);
                changed |= ImGui::MenuItem("Z Axis", nullptr, &grid.gridAxisZ);
                if (changed) ec.frame.chrome.display = grid;
                ImGui::EndMenu();
            }
            ImGui::Separator();
            GizmoVisibility& show = state.workspace.gizmos;
            ImGui::MenuItem("Lights",               nullptr, &show.lights);
            ImGui::MenuItem("Cameras",              nullptr, &show.cameras);
            ImGui::MenuItem("Audio",                nullptr, &show.audio);
            ImGui::MenuItem("Acoustics",            nullptr, &show.acoustics);
            ImGui::MenuItem("Probes and Volumes",   nullptr, &show.probes);
            ImGui::MenuItem("Decals and Emitters",  nullptr, &show.effects);
            ImGui::Separator();
            ImGui::MenuItem("Colliders and Joints", nullptr, &show.colliders);
            ImGui::MenuItem("Contacts",             nullptr, &show.contacts);
            ImGui::MenuItem("Bounds",               nullptr, &show.bounds);
            ImGui::MenuItem("Skeletons",            nullptr, &show.skeletons);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
}

void EditorMenuBar::drawWindowMenu(EditorState& state) {
    if (ImGui::BeginMenu("Window")) {
        Workspace& shown = state.workspace;
        ImGui::MenuItem("Hierarchy",    keyLabel(state.prefs.keybinds.toggleHierarchy), &shown.showHierarchy);
        ImGui::MenuItem("Inspector",    keyLabel(state.prefs.keybinds.toggleInspector), &shown.showInspector);
        ImGui::MenuItem("Assets",       keyLabel(state.prefs.keybinds.toggleAssets), &shown.showAssets);
        ImGui::MenuItem("Terrain",      keyLabel(state.prefs.keybinds.toggleTerrain), &shown.showTerrain);
        if (ImGui::MenuItem("Console", keyLabel(state.prefs.keybinds.showConsole))) {
            state.requests.ask(Ask::RevealConsole);
        }
        ImGui::Separator();
        ImGui::MenuItem(
            "Render Settings",
            keyLabel(state.prefs.keybinds.toggleRenderSettings),
            &state.showRenderSettings
        );
        ImGui::Separator();
        // The scene alone, the whole window; the key brings the editor back.
        if (ImGui::MenuItem("Hide Editor", keyLabel(state.prefs.keybinds.toggleEditor))) {
            state.requests.ask(Ask::HideEditor);
        }
        // Back to the layout a first launch opens on, with the panels it shows.
        if (ImGui::MenuItem("Reset Layout")) {
            state.requests.ask(Ask::ResetLayout);
            shown.showHierarchy = shown.showInspector = shown.showAssets = true;
        }
        ImGui::EndMenu();
    }
}

void EditorMenuBar::drawEntityMenu(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (ImGui::BeginMenu("Entity")) {
        CreateMenu::draw(ctx.scene, ctx.resources, state);
        ImGui::Separator();
        drawPlacingItems(ec);
        ImGui::Separator();
        // As the Hierarchy's menu offers it, on the selection; refused inside an instance with a toast.
        const bool canSave = ctx.scene.isAlive(state.selection.active()) && !ec.input.playing;
        if (ImGui::MenuItem("Save as Prefab", nullptr, false, canSave)) {
            EditorActions::saveAsPrefab(ctx.scene, ctx.resources, state, state.selection.active());
        }
        ImGui::EndMenu();
    }
}

void EditorMenuBar::drawHelpMenu() {
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Keyboard & Mouse", nullptr, &m_showShortcuts);
        // Deferred; drawAboutPopup says why.
        if (ImGui::MenuItem("About")) m_openAbout = true;
        ImGui::EndMenu();
    }
}

void EditorMenuBar::drawShortcutsWindow(EditorState& state) {
    if (!m_showShortcuts) return;
    const ImVec2 size(EditorStyle::px(460.0f), EditorStyle::px(560.0f));
    if (!beginToolWindow("Keyboard & Mouse", m_showShortcuts, size)) return;

    // The mouse's controls and these keys are fixed, not keybinds; CameraControllerSystem binds the view's.
    static constexpr const char* MOUSE[][2] = {
        {"Right drag",                 "Look; W A S D Q E fly, Shift faster"},
        {"Right drag + wheel",         "Fly speed"},
        {"Alt + left drag",            "Orbit the focus"},
        {"Middle drag",                "Pan"},
        {"Wheel",                      "Dolly toward the focus"},
        {"Left click",                 "Select; Shift adds, Ctrl toggles"},
        {"Alt + drag a gizmo handle",  "Copy the selection and move the copy"},
        {"Double-click an asset",      "Open it in its editor"},
        {"Drop a file on the editor",  "Import it into the project"},
        {"Right-click a card header",  "Reset, Copy, Paste Values, Revert to Prefab, Remove Component"},
        {"Ctrl + F in a panel",        "Its search; Escape empties it"},
    };
    const auto table = [](const char* id) {
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable(id, 2, flags)) return false;
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        return true;
    };
    const auto row = [](const char* input, const char* action) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(input);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", action);
    };

    ImGui::SeparatorText("Mouse and fixed keys");
    if (table("##mouse")) {
        for (const auto& entry : MOUSE) row(entry[0], entry[1]);
        const std::string rename = std::string(ImGui::GetKeyName(RENAME_KEY)) + " over the selected row or a loaded tile";
        row(rename.c_str(), "Rename it");
        row("Arrows in the Hierarchy", "Up and Down step the rows; Left folds or climbs, Right opens or steps in");
        ImGui::EndTable();
    }

    // In the keybind table's order, a heading each time its group changes.
    const char* group = nullptr;
    bool open = false;
    for (const KeybindEntry& entry : KEYBINDS) {
        if (!group || std::strcmp(group, entry.group) != 0) {
            if (open) ImGui::EndTable();
            group = entry.group;
            ImGui::SeparatorText(group);
            open = table(group);
        }
        if (open) row(keyLabel(state.prefs.keybinds.*entry.field).buf, entry.label);
    }
    if (open) ImGui::EndTable();

    ImGui::Spacing();
    if (ImGui::Button("Change in Preferences")) {
        state.showPreferences = true;
        ImGui::SetWindowFocus(PREFERENCES_WINDOW);
    }
    ImGui::End();
}

void EditorMenuBar::drawAboutPopup(EditorContext& ec) {
    if (m_openAbout) {
        ImGui::OpenPopup("##About");
        m_openAbout = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopup("##About")) {
        const float valueX = ImGui::CalcTextSize("vkmEngine:").x + EditorStyle::px(12.0f);
        const auto row = [valueX](const char* label, const char* value) {
            aboutRow(valueX, label, value);
        };

        ImGui::Text("%s  v%s", APP_NAME, APP_VERSION);
        ImGui::SameLine();
        ImGui::TextDisabled("%s (%s)", APP_BUILD_DATE, APP_BRANCH);
        ImGui::Separator();

        const BackendInfo backend = ec.renderSystem.backendInfo();
        row("API:",      backend.api.empty()    ? "(unknown)" : backend.api.c_str());
        row("Renderer:", backend.device.empty() ? "(unknown)" : backend.device.c_str());

        ImGui::Separator();

        // Collapsed by default: the commit hashes matter only when reproducing a report.
        ImGui::Spacing();
        if (beginFold("Debug", false)) {
            // A version, and the first eight characters of the commit it was built from.
            const auto versionRow = [&](const char* label, const char* version, const char* commit) {
                char value[64];
                std::snprintf(value, sizeof(value), "%s @ %.8s", version, commit);
                row(label, value);
            };
            versionRow("vkmEngine:",   APP_VERSION,          APP_COMMIT_HASH);
            versionRow("vkmLog:",      VKM_LOG_VERSION,      VKM_LOG_COMMIT_HASH);
            versionRow("vkmPlatform:", VKM_PLATFORM_VERSION, VKM_PLATFORM_COMMIT_HASH);
            versionRow("vkmAudio:",    VKM_AUDIO_VERSION,    VKM_AUDIO_COMMIT_HASH);
            versionRow("vkmNetwork:",  VKM_NETWORK_VERSION,  VKM_NETWORK_COMMIT_HASH);
            versionRow("vkmPhysics:",  VKM_PHYSICS_VERSION,  VKM_PHYSICS_COMMIT_HASH);
            versionRow("vkmOpenGL:",   VKM_OPENGL_VERSION,   VKM_OPENGL_COMMIT_HASH);
            ImGui::Spacing();
            row("ImGui:",     IMGUI_VERSION);
            endFold();
        }
        ImGui::EndPopup();
    }
}

void EditorMenuBar::draw(EditorContext& ec, BuildController& build) {
    if (!ImGui::BeginMenuBar()) return;

    EditorState& state = ec.state;

    if (drawEngineMark(ec.hooks, ImGui::GetTextLineHeight() * 1.5f)) ImGui::SameLine();

    // The start screen has no scene, panels or entities to act on.
    drawFileMenu(ec);
    drawEditMenu(ec);
    if (state.projectOpen) {
        drawViewMenu(ec);
        drawWindowMenu(ec.state);
        drawEntityMenu(ec);
    }
    drawHelpMenu();
    drawAboutPopup(ec);

    if (state.projectOpen) {
        drawTitle(state, ec.session);
        drawBuildChip(state, build);
    }
    ImGui::EndMenuBar();
    drawShortcutsWindow(state);
}

void EditorMenuBar::drawTitle(const EditorState& state, const SceneSession& session) {
    const std::filesystem::path scene(session.path());
    const std::string project = state.project.name.empty() ? "Untitled project" : state.project.name;
    const std::string stem    = session.hasPath() ? scene.stem().string() : "untitled";
    const char* between = "  >  ";

    const float projectW = ImGui::CalcTextSize(project.c_str()).x;
    const float betweenW = ImGui::CalcTextSize(between).x;
    const float stemW    = ImGui::CalcTextSize(stem.c_str()).x;
    const float dot      = EditorStyle::px(3.5f);
    const float dotGap   = EditorStyle::px(8.0f);
    const float width    = projectW + betweenW + stemW + dotGap + dot * 2.0f;

    // Centered on the bar, but never over the menus on a narrow window.
    const float menusEnd = ImGui::GetCursorPosX() + EditorStyle::px(16.0f);
    ImGui::SameLine(std::max((ImGui::GetWindowWidth() - width) * 0.5f, menusEnd));
    ImGui::BeginGroup();
    ImGui::TextUnformatted(project.c_str());
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextDisabled("%s%s", between, stem.c_str());
    ImGui::EndGroup();
    if (ImGui::IsItemHovered()) {
        const char* where = session.hasPath() ? session.path().c_str() : "The scene has not been saved yet";
        ImGui::SetTooltip("%s", where);
    }
    if (state.sceneDirty) {
        const ImVec2 end = ImGui::GetItemRectMax();
        const ImVec2 center(end.x + dotGap + dot, (ImGui::GetItemRectMin().y + end.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddCircleFilled(center, dot, ImGui::GetColorU32(EditorStyle::ACCENT));
    }
}

void EditorMenuBar::drawBuildChip(EditorState& state, BuildController& build) {
    // How long "Built" stays before the chip goes back to offering a build.
    constexpr double BUILT_SHOWN = 3.0;

    const KeyLabel key = keyLabel(state.prefs.keybinds.buildScripts);
    const char* shortcut = key.buf;
    const char* label    = "Build";
    ImVec4 tone = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const bool showing = build.running() || build.failed();
    if (build.running()) {
        label = "Building...";
        tone  = EditorStyle::ACCENT;
    } else if (build.failed()) {
        label = "Build failed";
        tone  = EditorStyle::DANGER;
    } else if (build.succeededWithin(BUILT_SHOWN)) {
        label = "Built";
        tone  = EditorStyle::SUCCESS;
    }

    // A 20 px pill: the word, and the key while it offers a build.
    const float  height = EditorStyle::px(20.0f);
    const float  pad    = EditorStyle::px(10.0f);
    const float  keyGap = EditorStyle::px(12.0f);
    const bool   offers = !showing && shortcut[0] != '\0';
    const float  labelW = ImGui::CalcTextSize(label).x;
    const float  keyW   = offers ? keyGap + ImGui::CalcTextSize(shortcut).x : 0.0f;
    const ImVec2 size(pad * 2.0f + labelW + keyW, height);
    ImGui::SameLine(ImGui::GetWindowWidth() - size.x - EditorStyle::px(10.0f));
    const ImVec2 line = ImGui::GetCursorScreenPos();
    const ImVec2 at(line.x, line.y + (ImGui::GetFrameHeight() - height) * 0.5f);
    ImGui::SetCursorScreenPos(at);
    const bool clicked = ImGui::InvisibleButton("##build", size);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + size.x, at.y + size.y);
    const ImVec4& fill = hovered ? EditorStyle::HOVERED : EditorStyle::RAISED;
    dl->AddRectFilled(at, end, ImGui::GetColorU32(fill), height * 0.5f);
    const float textY = at.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(at.x + pad, textY), ImGui::GetColorU32(tone), label);
    if (offers) {
        const ImVec2 keyAt(at.x + pad + labelW + keyGap, textY);
        dl->AddText(keyAt, ImGui::GetColorU32(ImGuiCol_TextDisabled), shortcut);
    }

    const bool canBuild = !BuildController::launcher().empty();
    if (showing) ImGui::SetItemTooltip("The vkm command - open the Console's Build view");
    else if (canBuild) ImGui::SetItemTooltip("Build the project's scripts with vkm");
    else ImGui::SetItemTooltip("This engine has no vkm beside it");
    if (!clicked) return;
    if (showing) build.reveal();
    else if (canBuild) state.requests.ask(VkmRun{{"build", Roots::projectRoot().string()}, {}});
}

} // namespace Vkm::Engine
