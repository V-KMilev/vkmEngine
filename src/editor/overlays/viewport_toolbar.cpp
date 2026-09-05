#include "overlays/viewport_toolbar.h"

#include "ecs/scene.h"
#include "framework/editor_common.h"
#include "framework/editor_context.h"
#include "system/render/render_system.h"
#include "framework/editor_actions.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

using EditorStyle::overlayButton;
using EditorStyle::overlayGap;
using EditorStyle::overlayGroupGap;
using EditorStyle::overlayInset;
using EditorStyle::overlayPad;

namespace {

void tipFor(char* buf, size_t n, const char* name, const KeyBind& bind) {
    char key[24];
    getKeyBindLabel(bind, key, sizeof(key));
    snprintf(buf, n, "%s  (%s)", name, key);
}
} // namespace

void ViewportToolbar::drawViewMode(EditorContext& ec) {
    RenderSettings& settings = ec.renderSystem.getSettings();

    ImGui::SetCursorPos(ImVec2(overlayInset(), overlayInset()));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(overlayPad(), overlayPad()));

    const float h = ImGui::GetFrameHeight() + overlayPad() * 2.0f;
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

    auto tool = [&](const char* id, EditorIcon icon, EditorTool which,
                    const char* name, const KeyBind& bind) {
        char tip[80];
        tipFor(tip, sizeof(tip), name, bind);
        if (iconButton(id, icon, state.tool == which, true, tip, overlayButton())) state.tool = which;
        ImGui::SameLine();
    };

    const float toolbarH = overlayButton() + overlayPad() * 2.0f + 2.0f;
    ImVec2 ws = ImGui::GetWindowSize();
    float padY = ImGui::GetStyle().WindowPadding.y;
    ImGui::SetCursorPos(ImVec2(overlayInset(), ws.y - padY - toolbarH - overlayInset()));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(overlayPad(), overlayPad()));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(overlayGap(), 0.0f));

    if (ImGui::BeginChild("##ViewportToolbar", ImVec2(0, toolbarH),
            ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_Borders)) {

        tool("sel", EditorIcon::Select,    EditorTool::Select,    "Select", kb.gizmoSelect);
        tool("mov", EditorIcon::Move,      EditorTool::Translate, "Move",   kb.gizmoTranslate);
        tool("rot", EditorIcon::Rotate,    EditorTool::Rotate,    "Rotate", kb.gizmoRotate);
        tool("scl", EditorIcon::Scale,     EditorTool::Scale,     "Scale",  kb.gizmoScale);

        ImGui::SameLine(0, overlayGroupGap());
        bool world = state.gizmoMode == GizmoMode::World;
        char spcTip[80];
        tipFor(spcTip, sizeof(spcTip), world ? "Space: World" : "Space: Local",
               kb.gizmoToggleSpace);
        if (iconButton("spc", world ? EditorIcon::SpaceWorld : EditorIcon::SpaceLocal,
                       false, true, spcTip, overlayButton()))
            state.gizmoMode = world ? GizmoMode::Local : GizmoMode::World;
        ImGui::SameLine();
        if (iconButton("snp", EditorIcon::Snap, state.snapEnabled, true,
                       "Grid snap (hold Ctrl for temporary)", overlayButton()))
            state.snapEnabled = !state.snapEnabled;

        bool haveSel = state.selectedEntity && ctx.scene.isAlive(state.selectedEntity);
        char dupTip[80], focTip[80], delTip[80], frameTip[80];
        tipFor(dupTip, sizeof(dupTip), "Duplicate", kb.duplicate);
        tipFor(focTip, sizeof(focTip), "Focus camera on selection", kb.focusSelected);
        tipFor(delTip, sizeof(delTip), "Delete", kb.deleteEntity);
        tipFor(frameTip, sizeof(frameTip), "Frame All", kb.frameAll);

        ImGui::SameLine(0, overlayGroupGap());
        if (iconButton("dup", EditorIcon::Duplicate, false, haveSel, dupTip, overlayButton()))
            EditorActions::duplicateSelection(ctx.scene, ctx.resources, state);
        ImGui::SameLine();
        if (iconButton("foc", EditorIcon::Focus, false, haveSel, focTip, overlayButton()))
            EditorActions::focusOnSelected(ctx, state, camera);
        ImGui::SameLine();
        if (iconButton("del", EditorIcon::Trash, false, haveSel, delTip, overlayButton()))
            EditorActions::deleteSelection(ctx.scene, state);

        ImGui::SameLine(0, overlayGroupGap());
        if (iconButton("frameAll", EditorIcon::FrameAll, false, true,
                       frameTip, overlayButton()))
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

} // namespace Vkm::Engine
