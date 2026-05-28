#include "overlays/viewport_toolbar.h"

#include "ecs/scene.h"
#include "framework/editor_common.h"
#include "io/screenshot.h"
#include "input/editor_actions.h"
#include "platform/window/window_manager.h"
#include "system/render/render_system.h"
#include "system/render/render_view.h"
#include "ui/editor_widgets.h"

namespace Engine {

namespace {
constexpr float BTN = 26.0f;   ///< Icon button side length
constexpr float SEP = 10.0f;   ///< Spacing between groups
constexpr float PAD = 5.0f;    ///< Toolbar inner padding

void tipFor(char* buf, size_t n, const char* name, const KeyBind* bind) {
    if (bind) {
        char key[24];
        getKeyBindLabel(*bind, key, sizeof(key));
        snprintf(buf, n, "%s  (%s)", name, key);
    } else {
        snprintf(buf, n, "%s", name);
    }
}
}

void ViewportToolbar::draw(EditorContext& ec) {
    FrameContext&     ctx    = ec.frame;
    EditorState&      state  = ec.state;
    CameraController& camera = ec.cameraController;

    const auto& kb = state.keybinds;

    auto tool = [&](const char* id, EditorIcon icon, GizmoOperation op,
                    const char* name, const KeyBind& bind) {
        char tip[80];
        tipFor(tip, sizeof(tip), name, &bind);
        if (iconButton(id, icon, state.gizmoOperation == op, true, tip, BTN))
            state.gizmoOperation = op;
        ImGui::SameLine();
    };

    // Top-left: standalone render-mode button. Sits alone in the corner so
    // it's the first thing the eye lands on when something looks wrong in
    // the viewport (e.g. the user left Overdraw on). Click opens a popup
    // with the same grouped picker the Environment Inspector uses.
    {
        ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
        EnvironmentConfig& env = ec.renderSystem.getEnvironment();
        const bool nonDefault = env.renderMode != RenderMode::Default;
        char modeBtnLabel[48];
        snprintf(modeBtnLabel, sizeof(modeBtnLabel), "%s###RenderModeBtn",
                 renderModeLabel(env.renderMode));
        if (nonDefault) {
            // Tint the button when not in Shaded mode so accidental
            // diagnostic-left-on is obvious - the viewport doesn't look
            // right and the corner badge tells you why.
            ImGui::PushStyleColor(ImGuiCol_Button,        EditorStyle::ACCENT);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  EditorStyle::ACCENT);
        }
        ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
        if (ImGui::Button(modeBtnLabel, ImVec2(0, BTN)))
            ImGui::OpenPopup("##RenderModePopup");
        ImGui::PopStyleColor();
        if (nonDefault) ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Render Mode  -  also in View > Render Settings > Diagnostics.\n"
                "Highlighted when not in Shaded.");

        if (ImGui::BeginPopup("##RenderModePopup")) {
            // Find the Environment-entity component so the change persists
            // (RenderSystem mirrors it each frame; the scene component is
            // the source of truth).
            EnvironmentConfig* sceneEnv = nullptr;
            ctx.scene.forEach<EnvironmentConfig>(
                [&](EntityId, EnvironmentConfig& e) { if (!sceneEnv) sceneEnv = &e; });
            if (sceneEnv) {
                RenderMode m = sceneEnv->renderMode;
                if (drawRenderModeMenuBody(m)) {
                    sceneEnv->renderMode = m;
                    ec.state.markSceneDirty();
                }
            } else {
                ImGui::TextDisabled("No Environment entity in scene.");
            }
            ImGui::EndPopup();
        }
    }

    constexpr float toolbarH = BTN + PAD * 2.0f + 2.0f;
    ImVec2 ws = ImGui::GetWindowSize();
    float padY = ImGui::GetStyle().WindowPadding.y;
    ImGui::SetCursorPos(ImVec2(8.0f, ws.y - padY - toolbarH - 8.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PAD, PAD));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));

    if (ImGui::BeginChild("##ViewportToolbar", ImVec2(0, toolbarH),
            ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_Borders)) {

        tool("sel", EditorIcon::Select,    GizmoOperation::Select,    "Select", kb.gizmoSelect);
        tool("mov", EditorIcon::Move,      GizmoOperation::Translate, "Move",   kb.gizmoTranslate);
        tool("rot", EditorIcon::Rotate,    GizmoOperation::Rotate,    "Rotate", kb.gizmoRotate);
        tool("scl", EditorIcon::Scale,     GizmoOperation::Scale,     "Scale",  kb.gizmoScale);

        ImGui::SameLine(0, SEP);
        bool world = state.gizmoMode == GizmoMode::World;
        char spcTip[80];
        tipFor(spcTip, sizeof(spcTip), world ? "Space: World" : "Space: Local",
               &kb.gizmoToggleSpace);
        if (iconButton("spc", world ? EditorIcon::SpaceWorld : EditorIcon::SpaceLocal,
                       false, true, spcTip, BTN))
            state.gizmoMode = world ? GizmoMode::Local : GizmoMode::World;
        ImGui::SameLine();
        if (iconButton("snp", EditorIcon::Snap, state.snapEnabled, true,
                       "Grid snap (hold Ctrl for temporary)", BTN))
            state.snapEnabled = !state.snapEnabled;

        bool haveSel = state.selectedEntity && ctx.scene.isAlive(state.selectedEntity);
        char dupTip[80], focTip[80], delTip[80];
        tipFor(dupTip, sizeof(dupTip), "Duplicate", &kb.duplicate);
        tipFor(focTip, sizeof(focTip), "Focus camera on selection", &kb.focusSelected);
        tipFor(delTip, sizeof(delTip), "Delete", &kb.deleteEntity);

        ImGui::SameLine(0, SEP);
        if (iconButton("dup", EditorIcon::Duplicate, false, haveSel, dupTip, BTN))
            EditorActions::duplicateEntity(ctx.scene, state, state.selectedEntity);
        ImGui::SameLine();
        if (iconButton("foc", EditorIcon::Focus, false, haveSel, focTip, BTN))
            EditorActions::focusOnSelected(ctx, state, camera);
        ImGui::SameLine();
        if (iconButton("del", EditorIcon::Trash, false, haveSel, delTip, BTN))
            EditorActions::deleteEntity(ctx.scene, state, state.selectedEntity);

        // Right group: scene-wide view actions.
        ImGui::SameLine(0, SEP);
        if (iconButton("frameAll", EditorIcon::FrameAll, false, true,
                       "Frame All  (Shift+F)", BTN))
            EditorActions::frameAll(ctx, camera);
        ImGui::SameLine();
        if (iconButton("shot", EditorIcon::Screenshot, false, true,
                       "Save viewport screenshot to APP_ROOT_DIR/screenshots/", BTN)) {
            Screenshot::captureViewport(ctx.window, ec.renderSystem.getBackend());
        }

        m_hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    } else {
        m_hovered = false;
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace Engine
