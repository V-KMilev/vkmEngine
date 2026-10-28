#include "chrome/editor_menu_bar.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

#include <imgui.h>

#include "chrome/dock_layout.h"
#include "core/system.h"
#include "core/clock.h"
#include "ecs/scene.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "editor_context.h"
#include "session/build_controller.h"
#include "session/scene_io_controller.h"
#include "editor_actions.h"
#include "input/view_framing.h"
#include "io/project_paths.h"
#include "system/render/render_backend.h"
#include "system/render/render_system.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {

// One label and value row of the About dialog. valueX is an offset from wherever
// the row starts, not from the window, so the rows inside the Debug tree stay
// aligned against its indent.
void aboutRow(float valueX, const char* label, const char* value) {
    const float startX = ImGui::GetCursorPosX();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(startX + valueX);
    ImGui::TextUnformatted(value);
}
// "Undo Transform" / "Redo" - the verb plus the top-of-stack op label, or just
// the verb when the stack end is reached (op == null).
void historyItemLabel(char* buf, size_t n, const char* verb, const char* op) {
    snprintf(buf, n, "%s%s%s", verb, op ? " " : "", op ? op : "");
}
} // namespace

void EditorMenuBar::drawSelectionItems(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    const bool haveSel = ctx.scene.isAlive(state.selectedEntity);
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
        state.deselect();
    }
}

void EditorMenuBar::drawPlacingItems(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    const bool haveSel = ctx.scene.isAlive(state.selectedEntity);
    const bool canMove = haveSel && ctx.scene.has<Transform>(state.selectedEntity);
    if (ImGui::MenuItem("Align With View", keyLabel(state.prefs.keybinds.alignWithView), false, canMove)) {
        EditorActions::alignWithView(ctx.scene, ctx.resources, state);
    }
    const bool canLook = canMove && ec.cameraController.isActive();
    if (ImGui::MenuItem("Align View to Selected", nullptr, false, canLook)) {
        ViewFraming::viewFromEntity(ctx.scene, state.selectedEntity, ec.cameraController);
    }
    if (ImGui::MenuItem("Drop to Ground", keyLabel(state.prefs.keybinds.dropToGround), false, canMove)) {
        EditorActions::dropToGround(ctx.scene, ctx.resources, state);
    }
}

void EditorMenuBar::drawFileMenu(EditorContext& ec, SceneIOController& sceneIO) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (ImGui::BeginMenu("File")) {
        const bool haveCurrent = sceneIO.hasPath();

        // Two bodies, one shape each: the heading carries the noun, so every row
        // drops it. Rows that then read alike across the two take a ## suffix -
        // ImGui hashes an id from the label, so two "Open..." would collide.
        ImGui::SeparatorText("Project");
        if (ImGui::MenuItem("New...")) state.requestNewProject = true;
        if (ImGui::MenuItem("Open...##project")) state.requestOpenProject = true;
        if (ImGui::BeginMenu("Open Recent##project", !state.recentProjects.empty())) {
            for (const std::string& p : state.recentProjects) {
                ImGui::PushID(p.c_str());
                const std::string shortName = std::filesystem::path(p).filename().string();
                if (ImGui::MenuItem(shortName.empty() ? p.c_str() : shortName.c_str())) {
                    state.requestSceneAction(EditorState::SceneAction::OpenProject, p);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Settings...##project")) state.showProjectSettings = true;
        ImGui::SeparatorText("Scene");
        if (ImGui::MenuItem("New", keyLabel(state.prefs.keybinds.newScene))) {
            state.requestSceneAction(EditorState::SceneAction::New);
        }
        if (ImGui::MenuItem("Open...##scene", keyLabel(state.prefs.keybinds.loadScene))) {
            sceneIO.requestLoad();
        }

        const bool haveRecents = !state.recentScenes.empty();
        if (ImGui::BeginMenu("Open Recent##scene", haveRecents)) {
            for (const auto& p : state.recentScenes) {
                const std::string shortName = std::filesystem::path(p).filename().string();
                ImGui::PushID(p.c_str());
                if (ImGui::MenuItem(shortName.c_str())) {
                    state.requestSceneAction(EditorState::SceneAction::Open, p);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear List")) state.recentScenes.clear();
            ImGui::EndMenu();
        }
        // Greyed during a play session rather than left to refuse itself: the
        // scene in the world is the simulation's copy, and writing it over the
        // authored file is the one save that cannot be taken back.
        const bool playing = sceneIO.isPlaying();
        const bool canSave = haveCurrent && !playing;
        if (ImGui::MenuItem("Save", keyLabel(state.prefs.keybinds.saveScene), false, canSave)) {
            sceneIO.save(ctx, state);
        }
        if (ImGui::MenuItem("Save As...", keyLabel(state.prefs.keybinds.saveSceneAs), false, !playing)) {
            sceneIO.requestSaveAs();
        }
        if (playing) ImGui::TextDisabled("Stop the play session to save");
        ImGui::Separator();
        // Run by vkm, in the Console's Build view; a build that changes the module reloads it.
        const bool canBuild = state.projectOpen && !BuildController::launcher().empty();
        const std::string root = ProjectPaths::projectRoot().string();
        const char* why = state.projectOpen ? "This engine has no vkm beside it" : "Open a project first";
        if (ImGui::MenuItem("Build Scripts", nullptr, false, canBuild)) {
            state.requestVkm = {"build", root};
        }
        if (!canBuild) ImGui::SetItemTooltip("%s", why);
        if (ImGui::MenuItem("Package Game...", nullptr, false, canBuild)) {
            state.requestVkm = {"package", root};
        }
        if (!canBuild) ImGui::SetItemTooltip("%s", why);
        if (ImGui::MenuItem("Reload Scripts", nullptr, false, state.projectOpen)) {
            state.requestScriptReload = true;
        }
        if (!state.projectOpen) ImGui::SetItemTooltip("Open a project first");

        if (haveCurrent) {
            ImGui::Separator();
            const std::string fname = std::filesystem::path(sceneIO.path()).filename().string();
            ImGui::TextDisabled("%s%s", fname.c_str(), state.sceneDirty ? "  (modified)" : "");
        }
        ImGui::Separator();
        // Requested rather than raising the window's close flag: the frame loop
        // reads that flag before the next frame begins, so the editor would be
        // gone before the close-intercept in the same stage could ask.
        if (ImGui::MenuItem("Exit")) state.requestSceneAction(EditorState::SceneAction::Quit);
        ImGui::EndMenu();
    }
}

void EditorMenuBar::drawHistory(EditorContext& ec) {
    FrameContext&       ctx      = ec.frame;
    EditorState&        state    = ec.state;
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
        if (ImGui::BeginMenu("History", anyHistory)) {
            drawHistory(ec);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        drawSelectionItems(ec);
        ImGui::Separator();
        // Shown and focused, not toggled: Preferences is a floating window, so
        // it can be open behind the ones beside it, and raising the flag alone
        // does nothing for a window that is already up underneath another.
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
        const bool canFrame = ec.cameraController.isActive();
        const bool canFrameSelected = canFrame && !!state.selectedEntity;
        const KeyLabel frameSelectedKey = keyLabel(state.prefs.keybinds.focusSelected);
        if (ImGui::MenuItem("Frame Selected", frameSelectedKey, false, canFrameSelected)) {
            ViewFraming::frameSelected(ctx, state.selection, ec.cameraController);
        }
        if (ImGui::MenuItem("Frame All", keyLabel(state.prefs.keybinds.frameAll), false, canFrame)) {
            ViewFraming::frameAll(ctx, ec.cameraController);
        }
        const KeyLabel orthoKey = keyLabel(state.prefs.keybinds.toggleOrthographic);
        if (ImGui::MenuItem("Orthographic", orthoKey, ec.cameraController.isOrthographic(), canFrame)) {
            ec.cameraController.setOrthographic(!ec.cameraController.isOrthographic());
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Show")) {
            GizmoVisibility& show = state.gizmos;
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
        ImGui::MenuItem("Hierarchy",    keyLabel(state.prefs.keybinds.toggleHierarchy), &state.showHierarchy);
        ImGui::MenuItem("Inspector",    keyLabel(state.prefs.keybinds.toggleInspector), &state.showInspector);
        ImGui::MenuItem("Assets",       keyLabel(state.prefs.keybinds.toggleAssets), &state.showAssets);
        ImGui::MenuItem("Terrain",      keyLabel(state.prefs.keybinds.toggleTerrain), &state.showTerrain);
        if (ImGui::MenuItem("Console", keyLabel(state.prefs.keybinds.showConsole))) {
            state.revealConsole = true;
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
            state.requestHideEditor = true;
        }
        // Back to the layout a first launch opens on, with the panels it shows.
        if (ImGui::MenuItem("Reset Layout")) {
            state.requestResetLayout = true;
            state.showHierarchy = state.showInspector = state.showAssets = true;
        }
        ImGui::EndMenu();
    }
}

void EditorMenuBar::drawEntityMenu(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (ImGui::BeginMenu("Entity")) {
        EditorActions::drawCreateEntityMenu(ctx.scene, ctx.resources, state);
        ImGui::Separator();
        drawPlacingItems(ec);
        ImGui::Separator();
        // As the Hierarchy's menu offers it, on the selection; refused inside an instance with a toast.
        const bool canSave = ctx.scene.isAlive(state.selectedEntity) && !ec.input.playing;
        if (ImGui::MenuItem("Save as Prefab", nullptr, false, canSave)) {
            EditorActions::saveAsPrefab(ctx.scene, ctx.resources, state, state.selectedEntity);
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

    // The mouse's controls are fixed, not keybinds; CameraControllerSystem binds the view's.
    static constexpr const char* MOUSE[][2] = {
        {"Right drag",                "Look; W A S D Q E fly, Shift faster"},
        {"Right drag + wheel",        "Fly speed"},
        {"Alt + left drag",           "Orbit the focus"},
        {"Middle drag",               "Pan"},
        {"Wheel",                     "Dolly toward the focus"},
        {"Left click",                "Select; Shift adds, Ctrl toggles"},
        {"Right-click a card header", "Reset, Copy, Paste Values"},
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

    ImGui::SeparatorText("Mouse");
    if (table("##mouse")) {
        for (const auto& entry : MOUSE) row(entry[0], entry[1]);
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

        // Collapsed by default: the hashes answer which commit of each module
        // this is, which matters when reproducing a report and not otherwise.
        ImGui::Spacing();
        if (ImGui::TreeNode("Debug")) {
            // A version, and the first eight characters of the commit it was built from.
            const auto versionRow = [&](const char* label, const char* version, const char* commit) {
                char value[64];
                std::snprintf(value, sizeof(value), "%s @ %.8s", version, commit);
                row(label, value);
            };
            versionRow("vkmEngine:", APP_VERSION,     APP_COMMIT_HASH);
            versionRow("vkmGL:",     VKM_GL_VERSION,  VKM_GL_COMMIT_HASH);
            versionRow("vkmLog:",    VKM_LOG_VERSION, VKM_LOG_COMMIT_HASH);
            ImGui::Spacing();
            row("ImGui:",     IMGUI_VERSION);
            ImGui::TreePop();
        }
        ImGui::EndPopup();
    }
}

void EditorMenuBar::draw(EditorContext& ec, SceneIOController& sceneIO) {
    if (!ImGui::BeginMenuBar()) return;

    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (drawEngineMark(ec.hooks, ImGui::GetTextLineHeight() * 1.5f)) ImGui::SameLine();

    // The start screen has no scene, panels or entities to act on.
    drawFileMenu(ec, sceneIO);
    drawEditMenu(ec);
    if (state.projectOpen) {
        drawViewMenu(ec);
        drawWindowMenu(ec.state);
        drawEntityMenu(ec);
    }
    drawHelpMenu();
    drawAboutPopup(ec);

    if (state.projectOpen) drawFrameRate(ctx.clock.getFrameRate());
    ImGui::EndMenuBar();
    drawShortcutsWindow(state);
}

void EditorMenuBar::drawFrameRate(float rate) {
    char fps[32];
    snprintf(fps, sizeof(fps), "%.0f FPS", rate);
    const float fpsW    = ImGui::CalcTextSize(fps).x;
    const float menuEnd = ImGui::GetCursorPosX();
    // Right-aligned, but never on top of the menus on a narrow window.
    const float rightAligned = ImGui::GetWindowWidth() - fpsW - EditorStyle::px(16.0f);
    const float pastMenus    = menuEnd + EditorStyle::px(12.0f);
    ImGui::SameLine(std::max(rightAligned, pastMenus));
    ImVec4 fpsColor = rate >= 60 ? EditorStyle::SUCCESS
        : rate >= 30 ? EditorStyle::WARNING
        : EditorStyle::DANGER;
    ImGui::PushStyleColor(ImGuiCol_Text, fpsColor);
    ImGui::TextUnformatted(fps);
    ImGui::PopStyleColor();
}

} // namespace Vkm::Engine
