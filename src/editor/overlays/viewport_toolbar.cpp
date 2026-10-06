#include "overlays/viewport_toolbar.h"

#include <imgui.h>

#include "ecs/scene.h"
#include "core/system.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "ui/editor_icons.h"
#include "ui/editor_style.h"
#include "editor_context.h"
#include "system/render/render_settings.h"
#include "input/view_framing.h"
#include "input/camera_controller_system.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

using EditorStyle::overlayButton;
using EditorStyle::overlayGap;
using EditorStyle::overlayGroupGap;
using EditorStyle::overlayInset;
using EditorStyle::overlayPad;
using EditorStyle::overlayStripHeight;

namespace {

void tipFor(char* buf, size_t n, const char* name, const KeyBind& bind) {
    char key[24];
    getKeyBindLabel(bind, key, sizeof(key));
    snprintf(buf, n, "%s  (%s)", name, key);
}

// The editor's own view or a scene camera as a read-only preview; greyed while a session shows the game's.
void drawViewCombo(EditorContext& ec) {
    CameraControllerSystem& camera = ec.cameraController;
    const Scene& scene = ec.frame.scene;
    const EntityId main = findActiveCamera(scene);

    char current[64] = "Editor";
    if (!camera.isActive()) snprintf(current, sizeof(current), "Game");
    else if (const EntityId through = camera.lookingThrough())
        getEntityDisplayName(scene, through, current, sizeof(current));

    ImGui::BeginDisabled(!camera.isActive());
    ImGui::SetNextItemWidth(EditorStyle::overlayComboWidth());
    if (beginCombo("##viewcamera", current)) {
        if (ImGui::Selectable("Editor", !camera.lookingThrough())) camera.lookThrough({});
        scene.forEach<Camera, Transform>([&](EntityId id, const Camera&, const Transform&) {
            char name[64];
            getEntityDisplayName(scene, id, name, sizeof(name));
            char label[80];
            snprintf(label, sizeof(label), id == main ? "%s  (main)" : "%s", name);
            ImGui::PushID(static_cast<int>(id.slot()));
            if (ImGui::Selectable(label, camera.lookingThrough() == id)) camera.lookThrough(id);
            ImGui::PopID();
        });
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !ImGui::IsItemActive()) {
        ImGui::SetTooltip(
            camera.isActive()
                ? "Look through the editor's view, or preview a scene camera"
                : "The game's camera - eject to look around"
        );
    }
}

// Combos are a frame tall and buttons a button tall; a combo sits on the buttons' centre line.
void centreOnButtonRow() {
    ImGui::SetCursorPosY(overlayPad() + (overlayButton() - ImGui::GetFrameHeight()) * 0.5f);
}

constexpr int TOOL_BUTTONS = 6;  // select, move, rotate, scale; space, snap

} // namespace

float ViewportToolbar::toolStripWidth() {
    return overlayButton() + EditorStyle::overlayStripChrome();
}

float ViewportToolbar::viewBarWidth() {
    return EditorStyle::overlayComboWidth() * 2.0f + overlayButton() * 3.0f
        + overlayGap() * 3.0f + overlayGroupGap() + EditorStyle::overlayStripChrome();
}

float ViewportToolbar::viewBarBottom() {
    return overlayInset() + overlayStripHeight();
}

void ViewportToolbar::drawViewBar(EditorContext& ec) {
    FrameContext&           ctx    = ec.frame;
    EditorState&            state  = ec.state;
    CameraControllerSystem& camera = ec.cameraController;
    const auto&             kb     = state.prefs.keybinds;

    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x - overlayInset() - viewBarWidth(), overlayInset()));
    if (beginOverlayStrip("##ViewportViewBar", ImVec2(viewBarWidth(), overlayStripHeight()))) {
        using Modes = Reflect::EnumNames<RenderMode>;
        int mode = static_cast<int>(ctx.render.renderMode);
        centreOnButtonRow();
        ImGui::SetNextItemWidth(EditorStyle::overlayComboWidth());
        if (comboList("##viewmode", &mode, Modes::values, static_cast<int>(Modes::count)))
            ctx.render.renderMode = static_cast<RenderMode>(mode);
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            ImGui::SetTooltip("Shading / debug view");

        ImGui::SameLine();
        centreOnButtonRow();
        drawViewCombo(ec);

        // The ones that fly the view go with it when it stands down for a session.
        const bool haveSel = state.selectedEntity && ctx.scene.isAlive(state.selectedEntity);
        const bool ortho   = camera.isOrthographic();
        char projTip[80], frameTip[80], focTip[80];
        const char* projName = ortho ? "Orthographic (to perspective)" : "Perspective (to orthographic)";
        tipFor(projTip, sizeof(projTip), projName, kb.toggleOrthographic);
        tipFor(frameTip, sizeof(frameTip), "Frame All", kb.frameAll);
        tipFor(focTip, sizeof(focTip), "Focus camera on selection", kb.focusSelected);
        ImGui::SameLine(0, overlayGroupGap());
        ImGui::SetCursorPosY(overlayPad());
        const EditorIcon projIcon = ortho ? EditorIcon::Orthographic : EditorIcon::Perspective;
        if (iconButton("proj", projIcon, false, camera.isActive(), projTip, overlayButton()))
            camera.setOrthographic(!ortho);
        ImGui::SameLine();
        if (iconButton("frameAll", EditorIcon::FrameAll, false, camera.isActive(), frameTip, overlayButton()))
            ViewFraming::frameAll(ctx, camera);
        ImGui::SameLine();
        const bool canFocus = haveSel && camera.isActive();
        if (iconButton("foc", EditorIcon::Focus, false, canFocus, focTip, overlayButton()))
            ViewFraming::frameSelected(ctx, state.selectedEntity, camera);

        m_viewBarHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
        );
    } else {
        m_viewBarHovered = false;
    }
    endOverlayStrip();
}

void ViewportToolbar::draw(EditorContext& ec) {
    EditorState& state = ec.state;
    const auto&  kb    = state.prefs.keybinds;

    auto tool = [&](
        const char* id,
        EditorIcon icon,
        EditorTool which,
        const char* name,
        const KeyBind& bind
    ) {
        char tip[80];
        tipFor(tip, sizeof(tip), name, bind);
        if (iconButton(id, icon, state.tool == which, true, tip, overlayButton())) state.tool = which;
    };

    const float height = overlayButton() * TOOL_BUTTONS + overlayGap() * (TOOL_BUTTONS - 2)
        + overlayGroupGap() + EditorStyle::overlayStripChrome();
    ImGui::SetCursorPos(ImVec2(overlayInset(), overlayInset()));
    if (beginOverlayStrip("##ViewportTools", ImVec2(toolStripWidth(), height))) {
        tool("sel", EditorIcon::Select, EditorTool::Select,    "Select", kb.gizmoSelect);
        tool("mov", EditorIcon::Move,   EditorTool::Translate, "Move",   kb.gizmoTranslate);
        tool("rot", EditorIcon::Rotate, EditorTool::Rotate,    "Rotate", kb.gizmoRotate);
        tool("scl", EditorIcon::Scale,  EditorTool::Scale,     "Scale",  kb.gizmoScale);

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - overlayGap() + overlayGroupGap());
        const bool world = state.gizmoMode == GizmoMode::World;
        char spcTip[80];
        tipFor(spcTip, sizeof(spcTip), world ? "Space: World" : "Space: Local", kb.gizmoToggleSpace);
        const EditorIcon spcIcon = world ? EditorIcon::SpaceWorld : EditorIcon::SpaceLocal;
        if (iconButton("spc", spcIcon, false, true, spcTip, overlayButton()))
            state.gizmoMode = world ? GizmoMode::Local : GizmoMode::World;
        const char* snapTip = "Grid snap (hold Ctrl for temporary)";
        if (iconButton("snp", EditorIcon::Snap, state.prefs.snapEnabled, true, snapTip, overlayButton()))
            state.prefs.snapEnabled = !state.prefs.snapEnabled;

        m_hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
        );
    } else {
        m_hovered = false;
    }
    endOverlayStrip();
}

} // namespace Vkm::Engine
