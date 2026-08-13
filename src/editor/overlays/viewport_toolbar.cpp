#include "overlays/viewport_toolbar.h"

#include "ecs/scene.h"
#include "framework/editor_common.h"
#include "framework/editor_context.h"
#include "system/render/render_system.h"
#include "framework/editor_actions.h"
#include "ui/editor_widgets.h"

namespace Engine {

namespace {
// Sizes in design px - font/DPI-relative via EditorStyle::px.
float BTN() { return EditorStyle::px(26.0f); }  ///< Icon button side length
float SEP() { return EditorStyle::px(10.0f); }  ///< Spacing between groups
float PAD() { return EditorStyle::px(5.0f);  }  ///< Toolbar inner padding

void tipFor(char* buf, size_t n, const char* name, const KeyBind& bind) {
    char key[24];
    getKeyBindLabel(bind, key, sizeof(key));
    snprintf(buf, n, "%s  (%s)", name, key);
}
} // namespace

void ViewportToolbar::drawViewMode(EditorContext& ec) {
    RenderSettings& settings = ec.renderSystem.getSettings();

    // Small top-left overlay: just the shading/debug view dropdown, styled
    // like the tool strip.
    ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PAD(), PAD()));

    const float h = ImGui::GetFrameHeight() + PAD() * 2.0f;
    if (ImGui::BeginChild("##ViewportViewMode", ImVec2(EditorStyle::px(160.0f), h),
            ImGuiChildFlags_Borders)) {
        ImGui::SetNextItemWidth(-1.0f);
        drawEnumCombo("##viewmode", settings.renderMode);
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            ImGui::SetTooltip("Shading / debug view");
    }
    m_viewModeHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void ViewportToolbar::draw(EditorContext& ec) {
    FrameContext&     ctx    = ec.frame;
    EditorState&      state  = ec.state;
    CameraControllerSystem& camera = ec.cameraController;

    const auto& kb = state.keybinds;

    auto tool = [&](const char* id, EditorIcon icon, GizmoOperation op,
                    const char* name, const KeyBind& bind) {
        char tip[80];
        tipFor(tip, sizeof(tip), name, bind);
        if (iconButton(id, icon, state.gizmoOperation == op, true, tip, BTN()))
            state.gizmoOperation = op;
        ImGui::SameLine();
    };

    const float toolbarH = BTN() + PAD() * 2.0f + 2.0f;
    ImVec2 ws = ImGui::GetWindowSize();
    float padY = ImGui::GetStyle().WindowPadding.y;
    ImGui::SetCursorPos(ImVec2(8.0f, ws.y - padY - toolbarH - 8.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PAD(), PAD()));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));

    if (ImGui::BeginChild("##ViewportToolbar", ImVec2(0, toolbarH),
            ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_Borders)) {

        tool("sel", EditorIcon::Select,    GizmoOperation::Select,    "Select", kb.gizmoSelect);
        tool("mov", EditorIcon::Move,      GizmoOperation::Translate, "Move",   kb.gizmoTranslate);
        tool("rot", EditorIcon::Rotate,    GizmoOperation::Rotate,    "Rotate", kb.gizmoRotate);
        tool("scl", EditorIcon::Scale,     GizmoOperation::Scale,     "Scale",  kb.gizmoScale);

        ImGui::SameLine(0, SEP());
        bool world = state.gizmoMode == GizmoMode::World;
        char spcTip[80];
        tipFor(spcTip, sizeof(spcTip), world ? "Space: World" : "Space: Local",
               kb.gizmoToggleSpace);
        if (iconButton("spc", world ? EditorIcon::SpaceWorld : EditorIcon::SpaceLocal,
                       false, true, spcTip, BTN()))
            state.gizmoMode = world ? GizmoMode::Local : GizmoMode::World;
        ImGui::SameLine();
        if (iconButton("snp", EditorIcon::Snap, state.snapEnabled, true,
                       "Grid snap (hold Ctrl for temporary)", BTN()))
            state.snapEnabled = !state.snapEnabled;

        bool haveSel = state.selectedEntity && ctx.scene.isAlive(state.selectedEntity);
        char dupTip[80], focTip[80], delTip[80], frameTip[80];
        tipFor(dupTip, sizeof(dupTip), "Duplicate", kb.duplicate);
        tipFor(focTip, sizeof(focTip), "Focus camera on selection", kb.focusSelected);
        tipFor(delTip, sizeof(delTip), "Delete", kb.deleteEntity);
        tipFor(frameTip, sizeof(frameTip), "Frame All", kb.frameAll);

        ImGui::SameLine(0, SEP());
        if (iconButton("dup", EditorIcon::Duplicate, false, haveSel, dupTip, BTN()))
            EditorActions::duplicateSelection(ctx.scene, state);
        ImGui::SameLine();
        if (iconButton("foc", EditorIcon::Focus, false, haveSel, focTip, BTN()))
            EditorActions::focusOnSelected(ctx, state, camera);
        ImGui::SameLine();
        if (iconButton("del", EditorIcon::Trash, false, haveSel, delTip, BTN()))
            EditorActions::deleteSelection(ctx.scene, state);

        // Right group: scene-wide view actions.
        ImGui::SameLine(0, SEP());
        if (iconButton("frameAll", EditorIcon::FrameAll, false, true,
                       frameTip, BTN()))
            EditorActions::frameAll(ctx, camera);

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
