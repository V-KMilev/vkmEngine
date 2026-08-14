#include "framework/editor_menu_bar.h"

#include "ui/editor_style.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <memory>

#include <imgui.h>

#include "texture/gl_texture.h"

#include "core/system.h"
#include "core/clock.h"
#include "framework/editor_common.h"
#include "framework/editor_context.h"
#include "framework/scene_io_controller.h"
#include "framework/editor_actions.h"
#include "io/project_paths.h"
#include "platform/window/window_manager.h"
#include "system/render/render_backend.h"
#include "system/render/render_system.h"
#include "ui/editor_widgets.h"

namespace Engine {

namespace {
// "Undo Transform" / "Redo" - the verb plus the top-of-stack op label, or just
// the verb when the stack end is reached (op == null).
void historyItemLabel(char* buf, size_t n, const char* verb, const char* op) {
    snprintf(buf, n, "%s%s%s", verb, op ? " " : "", op ? op : "");
}
} // namespace

// Defined here (not =default in the header) so the unique_ptr<Core::Texture2D>
// member sees the complete type for destruction.
EditorMenuBar::EditorMenuBar()  = default;
EditorMenuBar::~EditorMenuBar() = default;

void EditorMenuBar::draw(EditorContext& ec, SceneIOController& sceneIO) {
    if (!ImGui::BeginMenuBar()) return;

    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    // Brand mark at the left of the menu bar, before the menus. Lazy-loaded the
    // first time we draw (the GL context is live by now). Loaded unflipped so
    // ImGui's top-left UVs render it upright.
    if (!m_logo) {
        m_logo = std::make_unique<Core::Texture2D>(
            (ProjectPaths::assets() / "logo" / "vkm_engine_mark.png").string(),
            /*flipVertically*/ false);
    }
    if (m_logo->getWidth() > 0) {
        const float sz = ImGui::GetTextLineHeight();
        ImGui::Image(imTexture(m_logo->getID()), ImVec2(sz * 1.5f, sz * 1.5f));
        ImGui::SameLine();
    }

    if (ImGui::BeginMenu("File")) {
        const bool haveCurrent = sceneIO.hasPath();
        // A project is the bigger noun: it decides which scenes exist at all.
        if (ImGui::MenuItem("Open Project...")) state.showOpenProject = true;
        if (ImGui::BeginMenu("Recent Projects", !state.recentProjects.empty())) {
            for (const std::string& p : state.recentProjects) {
                ImGui::PushID(p.c_str());
                const std::string shortName = std::filesystem::path(p).filename().string();
                if (ImGui::MenuItem(shortName.empty() ? p.c_str() : shortName.c_str())) {
                    state.pendingProjectOpen = p;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("New Scene", keyLabel(state.keybinds.newScene))) {
            if (state.sceneDirty) state.confirmAction = EditorState::PendingSceneAction::New;
            else                  sceneIO.newScene(ctx, state);
        }
        if (ImGui::MenuItem("Open Scene...", keyLabel(state.keybinds.loadScene))) {
            sceneIO.requestLoad();
        }

        // Recent scenes: MRU list maintained by SceneIOController on every
        // save/load. Click loads through the same housekeeping path.
        const bool haveRecents = !state.recentScenes.empty();
        if (ImGui::BeginMenu("Open Recent", haveRecents)) {
            for (const auto& p : state.recentScenes) {
                const std::string shortName = std::filesystem::path(p).filename().string();
                ImGui::PushID(p.c_str());
                if (ImGui::MenuItem(shortName.c_str())) sceneIO.requestOpenPath(ctx, state, p);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear List")) state.recentScenes.clear();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Scene", keyLabel(state.keybinds.saveScene), false, haveCurrent)) {
            sceneIO.save(ctx, state);
        }
        if (ImGui::MenuItem("Save Scene As...", keyLabel(state.keybinds.saveSceneAs))) {
            sceneIO.requestSaveAs();
        }
        ImGui::Separator();
        // Hot-reload the gameplay module: rebuild game.dll, then click this to
        // swap the new code in without restarting (consumed by EditorSystem).
        if (ImGui::MenuItem("Reload Scripts")) {
            state.requestScriptReload = true;
        }

        if (haveCurrent) {
            ImGui::Separator();
            // Just the scene file name - the "Current:" prefix was noise.
            const std::string fname = std::filesystem::path(sceneIO.path()).filename().string();
            ImGui::TextDisabled("%s%s", fname.c_str(),
                state.sceneDirty ? "  (modified)" : "");
        }
        ImGui::Separator();
        // Exit routes through the window-close intercept, so the unsaved-
        // changes guard applies exactly as it does for the titlebar X.
        if (ImGui::MenuItem("Exit")) {
            ctx.window.requestClose();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        // Undo / redo with the top-of-stack label so users see what action
        // they're reverting (e.g. "Undo Transform", "Redo Create Entity").
        char undoText[80], redoText[80];
        historyItemLabel(undoText, sizeof(undoText), "Undo", state.commands.undoLabel());
        historyItemLabel(redoText, sizeof(redoText), "Redo", state.commands.redoLabel());
        if (ImGui::MenuItem(undoText, keyLabel(state.keybinds.undo), false, state.commands.canUndo())) {
            EditorActions::undo(ctx.scene, state);
        }
        if (ImGui::MenuItem(redoText, keyLabel(state.keybinds.redo), false, state.commands.canRedo())) {
            EditorActions::redo(ctx.scene, state);
        }
        ImGui::Separator();
        const bool haveSel = state.selectedEntity && ctx.scene.isAlive(state.selectedEntity);
        if (ImGui::MenuItem("Duplicate", keyLabel(state.keybinds.duplicate), false, haveSel)) {
            EditorActions::duplicateSelection(ctx.scene, state);
        }
        if (ImGui::MenuItem("Delete", keyLabel(state.keybinds.deleteEntity), false, haveSel)) {
            EditorActions::deleteSelection(ctx.scene, state);
        }
        if (ImGui::MenuItem("Deselect", keyLabel(state.keybinds.deselect), false, haveSel)) {
            state.deselect();
        }
        ImGui::Separator();
        // A dialog action, not a toggle - no checkmark.
        if (ImGui::MenuItem("Preferences...", keyLabel(state.keybinds.openPreferences))) {
            state.showPreferences = !state.showPreferences;
        }
        ImGui::EndMenu();
    }

    // View: camera / viewport commands only.
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Frame Selected", keyLabel(state.keybinds.focusSelected),
                            false, !!state.selectedEntity)) {
            EditorActions::focusOnSelected(ctx, state, ec.cameraController);
        }
        if (ImGui::MenuItem("Frame All", keyLabel(state.keybinds.frameAll))) {
            EditorActions::frameAll(ctx, ec.cameraController);
        }
        ImGui::Separator();
        ImGui::MenuItem("Show Colliders", nullptr, &state.showColliders);
        ImGui::MenuItem("Show Bounds",    nullptr, &state.showBounds);
        ImGui::EndMenu();
    }

    // Window: show/hide every panel and tool window. Docked layout panels
    // first, then the floating tool/settings windows.
    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Hierarchy",    keyLabel(state.keybinds.toggleHierarchy), &state.showHierarchy);
        ImGui::MenuItem("Inspector",    keyLabel(state.keybinds.toggleInspector), &state.showInspector);
        ImGui::MenuItem("Bottom Panel", keyLabel(state.keybinds.toggleBottom), &state.showBottom);
        ImGui::Separator();
        ImGui::MenuItem("Render Settings", keyLabel(state.keybinds.toggleRenderSettings), &state.showRenderSettings);
        ImGui::MenuItem("Material Editor", keyLabel(state.keybinds.toggleMaterialEditor), &state.showMaterialEditor);
        ImGui::MenuItem("Asset Browser",   keyLabel(state.keybinds.toggleAssetBrowser),   &state.showAssetBrowser);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Entity")) {
        EditorActions::drawCreateEntityMenu(ctx.scene, ctx.resources, state);
        ImGui::Separator();
        const bool haveSel = state.selectedEntity && ctx.scene.isAlive(state.selectedEntity);
        if (ImGui::MenuItem("Duplicate", keyLabel(state.keybinds.duplicate), false, haveSel)) {
            EditorActions::duplicateSelection(ctx.scene, state);
        }
        if (ImGui::MenuItem("Delete", keyLabel(state.keybinds.deleteEntity), false, haveSel)) {
            EditorActions::deleteSelection(ctx.scene, state);
        }
        if (ImGui::MenuItem("Deselect", keyLabel(state.keybinds.deselect), false, haveSel)) {
            state.deselect();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        // Defer the OpenPopup: calling it inside the menu scope hashes the id
        // against the menu's popup window, while the Begin below runs at menu-
        // bar scope - the ids never matched and About could never open.
        if (ImGui::MenuItem("About")) m_openAbout = true;
        ImGui::EndMenu();
    }

    if (m_openAbout) {
        ImGui::OpenPopup("##About");
        m_openAbout = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopup("##About")) {
        // Labels share a column so the values line up rather than stepping in
        // and out with the label width.
        const float valueX = ImGui::CalcTextSize("vkmEngine:").x + EditorStyle::px(12.0f);
        const auto row = [valueX](const char* label, const char* fmt, ...) {
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(valueX);
            va_list args;
            va_start(args, fmt);
            ImGui::TextV(fmt, args);
            va_end(args);
        };

        ImGui::Text("%s  v%s", APP_NAME, APP_VERSION);
        ImGui::SameLine();
        ImGui::TextDisabled("%s (%s)", APP_BUILD_DATE, APP_BRANCH);
        ImGui::Separator();

        // API + device come from the active render backend so this dialog
        // stays correct if the engine ever ships with a non-OpenGL backend.
        const BackendInfo backend = ec.renderSystem.backendInfo();
        row("API:",             "%s", backend.api.empty()    ? "(unknown)" : backend.api.c_str());
        row("Renderer:",        "%s", backend.device.empty() ? "(unknown)" : backend.device.c_str());

        ImGui::Separator();

        // Collapsed by default: the hashes answer "exactly which commit of each
        // module is this?", which matters when reproducing a report and not
        // otherwise. Each vkm module is its own repository, so one hash per
        // module rather than one for the tree.
        ImGui::Spacing();
        if (ImGui::TreeNode("Debug")) {
            row("vkmEngine:",    "%s @ %.8s", APP_VERSION,    APP_COMMIT_HASH);
            row("vkmGL:",        "%s @ %.8s", VKMGL_VERSION,  VKMGL_COMMIT_HASH);
            row("vkmLog:",       "%s @ %.8s", VKMLOG_VERSION, VKMLOG_COMMIT_HASH);
            ImGui::Spacing();
            row("ImGui:",        "%s", IMGUI_VERSION);
            ImGui::TreePop();
        }
        ImGui::EndPopup();
    }

    sceneIO.drawDialogs(ctx, state);
    // Model-import dialog is drawn at EditorSystem scope: it must be
    // available from multiple intent sources (Inspector empty-state,
    // Hierarchy "+"), not just the menu bar.

    const float rate = ctx.clock.getFrameRate();
    char fps[32];
    snprintf(fps, sizeof(fps), "%.0f FPS", rate);
    const float fpsW    = ImGui::CalcTextSize(fps).x;
    const float menuEnd = ImGui::GetCursorPosX();
    // Right-aligned, but never on top of the menus on a narrow window.
    ImGui::SameLine(std::max(ImGui::GetWindowWidth() - fpsW - EditorStyle::px(16.0f),
                             menuEnd + EditorStyle::px(12.0f)));
    ImVec4 fpsColor = rate >= 60 ? EditorStyle::SUCCESS :
                      rate >= 30 ? EditorStyle::WARNING :
                                   EditorStyle::DANGER;
    ImGui::PushStyleColor(ImGuiCol_Text, fpsColor);
    ImGui::TextUnformatted(fps);
    ImGui::PopStyleColor();

    ImGui::EndMenuBar();
}

} // namespace Engine
