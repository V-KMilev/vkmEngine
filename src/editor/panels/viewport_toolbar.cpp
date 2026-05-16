#include "viewport_toolbar.h"
#include "../editor_common.h"
#include "../editor_actions.h"

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

void ViewportToolbar::draw(FrameContext& ctx, EditorState& state, CameraController* camera) {
    const auto& kb = state.keybinds;

    auto tool = [&](const char* id, EditorIcon icon, GizmoOperation op,
                    const char* name, const KeyBind& bind) {
        char tip[48];
        tipFor(tip, sizeof(tip), name, &bind);
        if (iconButton(id, icon, state.gizmoOperation == op, true, tip, BTN))
            state.gizmoOperation = op;
        ImGui::SameLine();
    };

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
        char spcTip[48];
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
        char dupTip[48], focTip[48], delTip[48];
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
