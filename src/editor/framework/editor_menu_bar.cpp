#include "framework/editor_menu_bar.h"

#include <cstdio>

#include <imgui.h>

#include "core/system.h"
#include "debug/frame_tracker.h"
#include "framework/editor_common.h"
#include "framework/editor_context.h"
#include "framework/scene_io_controller.h"
#include "framework/editor_actions.h"
#include "system/render/render_backend.h"
#include "system/render/render_system.h"
#include "ui/editor_widgets.h"

namespace Engine {

void EditorMenuBar::draw(EditorContext& ec, SceneIOController& sceneIO) {
    if (!ImGui::BeginMenuBar()) return;

    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (ImGui::BeginMenu("File")) {
        char lbl[48];
        const bool haveCurrent = sceneIO.hasPath();
        if (ImGui::MenuItem("Save Scene",   getKeyBindLabel(state.keybinds.saveScene, lbl, sizeof(lbl)), false, haveCurrent)) {
            sceneIO.save(ctx, state);
        }
        if (ImGui::MenuItem("Save Scene As...", getKeyBindLabel(state.keybinds.saveSceneAs, lbl, sizeof(lbl)))) {
            sceneIO.requestSaveAs();
        }
        if (ImGui::MenuItem("Load Scene...", getKeyBindLabel(state.keybinds.loadScene, lbl, sizeof(lbl)))) {
            sceneIO.requestLoad();
        }

        // Recent scenes: MRU list maintained by SceneIOController on every
        // save/load. Click loads through the same housekeeping path.
        const bool haveRecents = !state.recentScenes.empty();
        if (ImGui::BeginMenu("Open Recent", haveRecents)) {
            for (const auto& p : state.recentScenes) {
                const size_t s = p.find_last_of("/\\");
                const char* shortName = (s == std::string::npos) ? p.c_str() : p.c_str() + s + 1;
                ImGui::PushID(p.c_str());
                if (ImGui::MenuItem(shortName)) sceneIO.loadPath(ctx, state, p);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear list")) state.recentScenes.clear();
            ImGui::EndMenu();
        }

        if (haveCurrent) {
            ImGui::Separator();
            // Just the scene file name - the "Current:" prefix was noise.
            const std::string& p = sceneIO.path();
            const size_t s = p.find_last_of("/\\");
            ImGui::TextDisabled("%s%s",
                s == std::string::npos ? p.c_str() : p.c_str() + s + 1,
                state.sceneDirty ? "  (modified)" : "");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        char lbl[48];
        // Undo / redo with the top-of-stack label so users see what action
        // they're reverting (e.g. "Undo Transform", "Redo Create Entity").
        char undoText[80], redoText[80];
        const char* undoOp = state.commands.undoLabel();
        const char* redoOp = state.commands.redoLabel();
        snprintf(undoText, sizeof(undoText), "Undo%s%s",
            undoOp ? " " : "", undoOp ? undoOp : "");
        snprintf(redoText, sizeof(redoText), "Redo%s%s",
            redoOp ? " " : "", redoOp ? redoOp : "");
        if (ImGui::MenuItem(undoText, getKeyBindLabel(state.keybinds.undo, lbl, sizeof(lbl)),
                false, state.commands.canUndo())) {
            state.commands.undo(ctx.scene, state);
            state.markSceneDirty();
        }
        if (ImGui::MenuItem(redoText, getKeyBindLabel(state.keybinds.redo, lbl, sizeof(lbl)),
                false, state.commands.canRedo())) {
            state.commands.redo(ctx.scene, state);
            state.markSceneDirty();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Preferences...",
                getKeyBindLabel(state.keybinds.openPreferences, lbl, sizeof(lbl)),
                state.showPreferences)) {
            state.showPreferences = !state.showPreferences;
        }
        ImGui::EndMenu();
    }

    // View: camera / viewport commands only.
    if (ImGui::BeginMenu("View")) {
        char lbl[48];
        if (ImGui::MenuItem("Frame Selected", getKeyBindLabel(state.keybinds.focusSelected, lbl, sizeof(lbl)),
                            false, !!state.selectedEntity)) {
            EditorActions::focusOnSelected(ctx, state, ec.cameraController);
        }
        if (ImGui::MenuItem("Frame All", "Shift+F")) {
            EditorActions::frameAll(ctx, ec.cameraController);
        }
        ImGui::EndMenu();
    }

    // Window: show/hide every panel and tool window. Docked layout panels
    // first, then the floating tool/settings windows.
    if (ImGui::BeginMenu("Window")) {
        char lbl[48];
        ImGui::MenuItem("Scene",         getKeyBindLabel(state.keybinds.toggleHierarchy, lbl, sizeof(lbl)), &state.showHierarchy);
        ImGui::MenuItem("Inspector",     getKeyBindLabel(state.keybinds.toggleInspector, lbl, sizeof(lbl)), &state.showInspector);
        ImGui::MenuItem("Bottom Panel",  getKeyBindLabel(state.keybinds.toggleBottom, lbl, sizeof(lbl)), &state.showBottom);
        ImGui::Separator();
        ImGui::MenuItem("Render Settings",  nullptr, &state.showRenderSettings);
        ImGui::MenuItem("Physics Settings", nullptr, &state.showPhysics);
        ImGui::MenuItem("Material Editor",  nullptr, &state.showMaterialEditor);
        ImGui::MenuItem("Asset Browser",    nullptr, &state.showAssetBrowser);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Entity")) {
        EditorActions::drawCreateEntityMenu(ctx.scene, ctx.resources, state);
        ImGui::Separator();
        char deselectLbl[48];
        if (ImGui::MenuItem("Deselect", getKeyBindLabel(state.keybinds.deselect, deselectLbl, sizeof(deselectLbl)))) {
            state.selectedEntity = {};
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About")) ImGui::OpenPopup("##About");
        ImGui::EndMenu();
    }

    if (ImGui::BeginPopup("##About")) {
        ImGui::Text("%s  v%s", APP_NAME, APP_VERSION);
        ImGui::Separator();
        ImGui::TextDisabled("Branch:");   ImGui::SameLine(); ImGui::Text("%s", APP_BRANCH);
        ImGui::TextDisabled("Commit:");   ImGui::SameLine(); ImGui::Text("%.8s", APP_COMMIT_HASH);
        ImGui::TextDisabled("Built:");    ImGui::SameLine(); ImGui::Text("%s", APP_BUILD_DATE);
        // API + device come from the active render backend so this dialog
        // stays correct if the engine ever ships with a non-OpenGL backend.
        RenderBackend& backend = ec.renderSystem.getBackend();
        const std::string ver = backend.apiVersion();
        const std::string dev = backend.deviceName();
        ImGui::TextDisabled("%s:", backend.apiName());
        ImGui::SameLine(); ImGui::Text("%s", ver.empty() ? "(unknown)" : ver.c_str());
        ImGui::TextDisabled("Renderer:");
        ImGui::SameLine(); ImGui::Text("%s", dev.empty() ? "(unknown)" : dev.c_str());
        ImGui::TextDisabled("ImGui:");    ImGui::SameLine(); ImGui::Text("%s", IMGUI_VERSION);
        ImGui::EndPopup();
    }

    sceneIO.drawDialogs(ctx, state);
    // Model-import dialog is drawn at EditorSystem scope: it must be
    // available from multiple intent sources (Inspector empty-state,
    // Hierarchy "+"), not just the menu bar.

    const float rate = ctx.frameTracker.getFrameRateInfo().frameRate;
    char fps[32];
    snprintf(fps, sizeof(fps), "%.0f FPS", rate);
    float fpsW = ImGui::CalcTextSize(fps).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - fpsW - 16.0f);
    ImVec4 fpsColor = rate >= 60 ? ImVec4(0.4f, 0.8f, 0.4f, 1.0f) :
                      rate >= 30 ? ImVec4(0.9f, 0.8f, 0.3f, 1.0f) :
                                   ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, fpsColor);
    ImGui::TextUnformatted(fps);
    ImGui::PopStyleColor();

    ImGui::EndMenuBar();
}

} // namespace Engine
