#include "framework/editor_menu_bar.h"
#include "framework/editor_context.h"
#include "framework/editor_common.h"
#include "framework/scene_io_controller.h"
#include "input/editor_actions.h"
#include "ui/editor_widgets.h"

#include <imgui.h>
#include <GL/glew.h>

#include <cstdio>

#include "core/system.h"

namespace Engine {

void EditorMenuBar::draw(EditorContext& ec, SceneIOController& sceneIO) {
    if (!ImGui::BeginMenuBar()) return;

    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (ImGui::BeginMenu("File")) {
        char lbl[48];
        const bool haveCurrent = sceneIO.hasPath();
        if (ImGui::MenuItem("Save Scene",   getKeyBindLabel(state.keybinds.saveScene, lbl, sizeof(lbl)), false, haveCurrent || true)) {
            sceneIO.save(ctx);
        }
        if (ImGui::MenuItem("Save Scene As...", getKeyBindLabel(state.keybinds.saveSceneAs, lbl, sizeof(lbl)))) {
            sceneIO.requestSaveAs();
        }
        if (ImGui::MenuItem("Load Scene...", getKeyBindLabel(state.keybinds.loadScene, lbl, sizeof(lbl)))) {
            sceneIO.requestLoad();
        }
        if (haveCurrent) {
            ImGui::Separator();
            // Just the scene file name - the "Current:" prefix was noise.
            const std::string& p = sceneIO.path();
            const size_t s = p.find_last_of("/\\");
            ImGui::TextDisabled("%s",
                s == std::string::npos ? p.c_str() : p.c_str() + s + 1);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        char lbl[48];
        if (ImGui::MenuItem("Preferences...",
                getKeyBindLabel(state.keybinds.openPreferences, lbl, sizeof(lbl)),
                state.showPreferences)) {
            state.showPreferences = !state.showPreferences;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        char lbl[48];
        ImGui::MenuItem("Stats Overlay", getKeyBindLabel(state.keybinds.toggleStats, lbl, sizeof(lbl)), &state.showStats);
        ImGui::MenuItem("Hierarchy",     getKeyBindLabel(state.keybinds.toggleHierarchy, lbl, sizeof(lbl)), &state.showHierarchy);
        ImGui::MenuItem("Inspector",     getKeyBindLabel(state.keybinds.toggleInspector, lbl, sizeof(lbl)), &state.showInspector);
        ImGui::MenuItem("Bottom Panel",  getKeyBindLabel(state.keybinds.toggleBottom, lbl, sizeof(lbl)), &state.showBottom);
        ImGui::Separator();
        ImGui::MenuItem("Material Editor", nullptr, &state.showMaterialEditor);
        ImGui::MenuItem("Asset Browser",   nullptr, &state.showAssetBrowser);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Scene")) {
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
        ImGui::TextDisabled("OpenGL:");   ImGui::SameLine(); ImGui::Text("%s", (const char*)glGetString(GL_VERSION));
        ImGui::TextDisabled("Renderer:"); ImGui::SameLine(); ImGui::Text("%s", (const char*)glGetString(GL_RENDERER));
        ImGui::TextDisabled("ImGui:");    ImGui::SameLine(); ImGui::Text("%s", IMGUI_VERSION);
        ImGui::EndPopup();
    }

    sceneIO.drawDialogs(ctx, state);
    EditorActions::drawModelImportDialog(ctx.scene, ctx.resources, state);

    const auto& info = ctx.statistics.getFrameInfo();
    char fps[32];
    snprintf(fps, sizeof(fps), "%.0f FPS", info.frameRateInfo.frameRate);
    float fpsW = ImGui::CalcTextSize(fps).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - fpsW - 16.0f);
    float rate = info.frameRateInfo.frameRate;
    ImVec4 fpsColor = rate >= 60 ? ImVec4(0.4f, 0.8f, 0.4f, 1.0f) :
                      rate >= 30 ? ImVec4(0.9f, 0.8f, 0.3f, 1.0f) :
                                   ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, fpsColor);
    ImGui::TextUnformatted(fps);
    ImGui::PopStyleColor();

    ImGui::EndMenuBar();
}

} // namespace Engine
