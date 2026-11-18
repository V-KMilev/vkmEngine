#include "overlays/viewport_toolbar.h"

#include <cstddef>
#include <cstdio>

#include <imgui.h>

#include "ecs/scene.h"
#include "core/host_chrome.h"
#include "core/frame_context.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/render/camera.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "ui/style/editor_icons.h"
#include "ui/style/editor_style.h"
#include "editor_context.h"
#include "core/render_mode.h"
#include "view/view_framing.h"
#include "view/camera_controller_system.h"
#include "ui/editor_widgets.h"
#include "ui/entity_labels.h"
#include "overlays/overlay_strip.h"

namespace Vkm::Engine {

namespace {

void tipFor(char* buf, size_t n, const char* name, const KeyBind& bind) {
    char key[24];
    getKeyBindLabel(bind, key, sizeof(key));
    snprintf(buf, n, "%s  (%s)", name, key);
}

// The editor's own view or a scene camera as a read-only preview; grayed while a session shows the game's.
void drawViewCombo(EditorContext& ec) {
    CameraControllerSystem& camera = ec.cameraController;
    const Scene& scene = ec.frame.scene;
    const EntityId main = findActiveCamera(scene);

    char current[64] = "Editor";
    if (!camera.isActive()) snprintf(current, sizeof(current), "Game");
    else if (const EntityId through = camera.lookingThrough())
        getEntityDisplayName(scene, through, current, sizeof(current));

    ImGui::BeginDisabled(!camera.isActive());
    ImGui::SetNextItemWidth(OverlayStrip::comboWidth());
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

// Combos are a frame tall and buttons a button tall; a combo sits on the buttons' center line.
void centerOnButtonRow() {
    ImGui::SetCursorPosY(OverlayStrip::pad() + (OverlayStrip::button() - ImGui::GetFrameHeight()) * 0.5f);
}

constexpr int TOOL_BUTTONS = 6;  // select, move, rotate, scale; space, snap

} // namespace

float ViewportToolbar::toolStripWidth() {
    return OverlayStrip::button() + OverlayStrip::chrome();
}

float ViewportToolbar::viewBarWidth() {
    return OverlayStrip::comboWidth() * 2.0f + OverlayStrip::button() * 4.0f
        + OverlayStrip::gap() * 4.0f + OverlayStrip::groupGap() + OverlayStrip::chrome();
}

float ViewportToolbar::viewBarBottom() {
    return OverlayStrip::inset() + OverlayStrip::height();
}

namespace {

// A square button in its axis's color, filled while @p shown and outlined while not.
bool axisToggle(int axis, bool* shown) {
    static constexpr const char* NAMES[3] = {"X", "Y", "Z"};
    const ImVec4 color    = EditorStyle::axisVec4(axis);
    const ImVec4 hover    = EditorStyle::axisVec4(axis, 25);
    const ImVec4 off      = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 offHover = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
    ImGui::PushStyleColor(ImGuiCol_Button, *shown ? color : off);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, *shown ? hover : offHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, hover);
    ImGui::PushStyleColor(ImGuiCol_Text, *shown ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : hover);
    const float height  = ImGui::GetFrameHeight();
    const bool  clicked = ImGui::Button(NAMES[axis], ImVec2(height * 1.5f, height));
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s the %s axis", *shown ? "Hide" : "Show", NAMES[axis]);
    if (clicked) *shown = !*shown;
    return clicked;
}

// The grid's switches, one per axis; two on show their plane.
bool gridAxes(HostChrome::Display& shown) {
    bool* axes[3] = {&shown.gridAxisX, &shown.gridAxisY, &shown.gridAxisZ};
    bool  changed = false;
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0, EditorStyle::px(4.0f));
        changed |= axisToggle(i, axes[i]);
    }
    return changed;
}

} // namespace

void ViewportToolbar::drawViewBar(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    EditorState& state = ec.state;
    CameraControllerSystem& camera = ec.cameraController;
    const auto& kb = state.prefs.keybinds;

    const float top = ImGui::GetCursorStartPos().y + OverlayStrip::inset();
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x - OverlayStrip::inset() - viewBarWidth(), top));
    if (OverlayStrip::begin("##ViewportViewBar", ImVec2(viewBarWidth(), OverlayStrip::height()))) {
        using Modes = Reflect::EnumNames<RenderMode>;
        HostChrome::Display shown = ctx.chrome.display;
        int mode = static_cast<int>(shown.mode);
        centerOnButtonRow();
        ImGui::SetNextItemWidth(OverlayStrip::comboWidth());
        if (comboList("##viewmode", &mode, Modes::values, static_cast<int>(Modes::count))) {
            shown.mode         = static_cast<RenderMode>(mode);
            ctx.chrome.display = shown;
        }
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            ImGui::SetTooltip("Shading / debug view");

        ImGui::SameLine();
        centerOnButtonRow();
        drawViewCombo(ec);

        ImGui::SameLine();
        ImGui::SetCursorPosY(OverlayStrip::pad());
        if (iconButton("grid", EditorIcon::Grid, shown.gridShown(), true, "Grid", OverlayStrip::button())) {
            ImGui::OpenPopup("##GridAxes");
        }
        if (ImGui::BeginPopup("##GridAxes")) {
            sectionLabel("Grid");
            if (gridAxes(shown)) ctx.chrome.display = shown;
            ImGui::TextDisabled("Two axes on show their plane: X and Z the ground.");
            ImGui::EndPopup();
        }

        // The ones that fly the view go with it when it stands down for a session.
        const bool haveSel = state.selection.active() && ctx.scene.isAlive(state.selection.active());
        const bool ortho   = camera.isOrthographic();
        char projTip[80], frameTip[80], focTip[80];
        const char* projName = ortho ? "Orthographic (to perspective)" : "Perspective (to orthographic)";
        tipFor(projTip, sizeof(projTip), projName, kb.toggleOrthographic);
        tipFor(frameTip, sizeof(frameTip), "Frame All", kb.frameAll);
        tipFor(focTip, sizeof(focTip), "Focus camera on selection", kb.focusSelected);
        ImGui::SameLine(0, OverlayStrip::groupGap());
        ImGui::SetCursorPosY(OverlayStrip::pad());
        const EditorIcon projIcon = ortho ? EditorIcon::Orthographic : EditorIcon::Perspective;
        if (iconButton("proj", projIcon, false, camera.isActive(), projTip, OverlayStrip::button()))
            camera.setOrthographic(!ortho);
        ImGui::SameLine();
        if (iconButton(
            "frameAll",
            EditorIcon::FrameAll,
            false,
            camera.isActive(),
            frameTip,
            OverlayStrip::button()
        ))
            ViewFraming::frameAll(ctx, camera);
        ImGui::SameLine();
        const bool canFocus = haveSel && camera.isActive();
        if (iconButton("foc", EditorIcon::Focus, false, canFocus, focTip, OverlayStrip::button()))
            ViewFraming::frameSelected(ctx, state.selection.entities(), camera);

        m_viewBarHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
        );
    } else {
        m_viewBarHovered = false;
    }
    OverlayStrip::end();
}

namespace {

// The steps the snap button's menu offers; Preferences takes any other.
constexpr float MOVE_STEPS[]   = {0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f};       ///< Meters.
constexpr float ROTATE_STEPS[] = {5.0f, 10.0f, 15.0f, 22.5f, 30.0f, 45.0f, 90.0f};  ///< Degrees.
constexpr float SCALE_STEPS[]  = {0.05f, 0.1f, 0.25f, 0.5f, 1.0f};                  ///< Of the size it has.

/// One row of steps, the current one ticked.
template <size_t N>
void stepRow(const char* heading, const char* unit, const float (&steps)[N], float& step) {
    ImGui::SeparatorText(heading);
    for (const float choice : steps) {
        char label[24];
        snprintf(label, sizeof(label), "%g%s", choice, unit);
        if (ImGui::MenuItem(label, nullptr, step == choice)) step = choice;
    }
}

} // namespace

void ViewportToolbar::drawSnapSteps(Preferences& prefs) {
    if (!ImGui::BeginPopup("##SnapSteps")) return;
    stepRow("Move", " m", MOVE_STEPS, prefs.snapTranslate);
    stepRow("Rotate", " deg", ROTATE_STEPS, prefs.snapRotate);
    stepRow("Scale", "", SCALE_STEPS, prefs.snapScale);
    ImGui::EndPopup();
}

void ViewportToolbar::draw(EditorContext& ec) {
    // The tools act on the view, which the game holds while it plays: their strip would only cover
    // the game's own corner.
    if (ec.input.gameHasViewport()) {
        m_hovered = false;
        return;
    }
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
        EditorTool& active = state.workspace.tool;
        if (iconButton(id, icon, active == which, true, tip, OverlayStrip::button())) active = which;
    };

    const float height = OverlayStrip::button() * TOOL_BUTTONS + OverlayStrip::gap() * (TOOL_BUTTONS - 2)
        + OverlayStrip::groupGap() + OverlayStrip::chrome();
    ImGui::SetCursorPos(ImVec2(OverlayStrip::inset(), OverlayStrip::inset()));
    if (OverlayStrip::begin("##ViewportTools", ImVec2(toolStripWidth(), height))) {
        tool("sel", EditorIcon::Select, EditorTool::Select,    "Select", kb.gizmoSelect);
        tool("mov", EditorIcon::Move,   EditorTool::Translate, "Move",   kb.gizmoTranslate);
        tool("rot", EditorIcon::Rotate, EditorTool::Rotate,    "Rotate", kb.gizmoRotate);
        tool("scl", EditorIcon::Scale,  EditorTool::Scale,     "Scale",  kb.gizmoScale);

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - OverlayStrip::gap() + OverlayStrip::groupGap());
        const bool world = state.workspace.gizmoMode == GizmoMode::World;
        char spcTip[80];
        tipFor(spcTip, sizeof(spcTip), world ? "Space: World" : "Space: Local", kb.gizmoToggleSpace);
        const EditorIcon spcIcon = world ? EditorIcon::SpaceWorld : EditorIcon::SpaceLocal;
        if (iconButton("spc", spcIcon, false, true, spcTip, OverlayStrip::button()))
            state.workspace.gizmoMode = world ? GizmoMode::Local : GizmoMode::World;
        Preferences& prefs = state.prefs;
        char snapTip[160];
        snprintf(
            snapTip,
            sizeof(snapTip),
            "Grid snap: %g m, %g deg, %g (hold Ctrl for temporary; right-click for the steps)",
            prefs.snapTranslate,
            prefs.snapRotate,
            prefs.snapScale
        );
        if (iconButton("snp", EditorIcon::Snap, prefs.snapEnabled, true, snapTip, OverlayStrip::button()))
            prefs.snapEnabled = !prefs.snapEnabled;
        ImGui::OpenPopupOnItemClick("##SnapSteps", ImGuiPopupFlags_MouseButtonRight);
        drawSnapSteps(prefs);

        m_hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
        );
    } else {
        m_hovered = false;
    }
    OverlayStrip::end();
}

} // namespace Vkm::Engine
