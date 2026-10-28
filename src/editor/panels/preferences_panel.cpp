#include "panels/preferences_panel.h"

#include <algorithm>
#include <cstring>
#include <string>

#include <imgui.h>

#include "chrome/dock_layout.h"
#include "core/system.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_widgets.h"
#include "ui/editor_style.h"

#include "platform/threading/thread_pool.h"
#include "platform/window/window_manager.h"
#include "input/camera_controller_system.h"

namespace Vkm::Engine {

void PreferencesPanel::draw(EditorContext& ec) {
    EditorState& state = ec.state;

    // A capture lives only while its row is on screen: every shortcut stands down for it.
    bool keybindsShown = false;
    const ImVec2 size(EditorStyle::px(620.0f), EditorStyle::px(480.0f));
    if (!beginToolWindow(PREFERENCES_WINDOW, state.showPreferences, size)) {
        m_rebindTarget = nullptr;
        return;
    }

    // Only on appearing: the field is an edit buffer, and re-reading it would undo typing.
    if (ImGui::IsWindowAppearing()) m_fpsLimitEdit = state.prefs.fpsCap;

    if (ImGui::BeginTabBar("##PrefTabs", ImGuiTabBarFlags_DrawSelectedOverline)) {
        if (ImGui::BeginTabItem("Camera")) {
            ImGui::Spacing();
            drawCameraSection(ec);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gizmo")) {
            ImGui::Spacing();
            drawGizmoSection(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Display")) {
            ImGui::Spacing();
            drawDisplaySection(ec);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Keybinds")) {
            ImGui::Spacing();
            drawKeybindsSection(state);
            keybindsShown = true;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (!keybindsShown) m_rebindTarget = nullptr;

    ImGui::End();
}

void PreferencesPanel::drawCameraSection(EditorContext& ec) {
    using Settings = CameraControllerSystem::Settings;
    auto& s = ec.state.prefs.camera;
    propDrag("Move Speed", &s.moveSpeed, 0.5f, Settings::MIN_MOVE_SPEED, Settings::MAX_MOVE_SPEED);
    const float boostLeast = Settings::MIN_SPEED_BOOST;
    propDrag("Speed Boost", &s.speedBoost, 0.1f, boostLeast, Settings::MAX_SPEED_BOOST, "%.1fx");
    propDrag("Look Speed", &s.lookSensitivity, 0.0001f, Settings::MIN_LOOK, Settings::MAX_LOOK, "%.4f");
    propDrag("Dolly Step", &s.dollyStep, 0.01f, Settings::MIN_DOLLY_STEP, Settings::MAX_DOLLY_STEP, "%.2f");
    const float pitchLimit = Settings::PITCH_LIMIT;
    propDrag("Min Pitch", &s.minPitch, 0.5f, -pitchLimit, 0.0f, "%.0f deg");
    propDrag("Max Pitch", &s.maxPitch, 0.5f, 0.0f, pitchLimit, "%.0f deg");
    ImGui::Spacing();
    if (ImGui::Button("Reset to Defaults")) s = CameraControllerSystem::Settings{};
}

void PreferencesPanel::drawGizmoSection(EditorState& state) {
    sectionLabel("Snapping");
    propCheckbox("Snap Enabled", &state.prefs.snapEnabled, "Hold Ctrl to snap while it is off");

    ImGui::Spacing();
    Preferences& p = state.prefs;
    propDrag("Translate", &p.snapTranslate, 0.1f, p.MIN_SNAP_TRANSLATE, p.MAX_SNAP_TRANSLATE, "%.2f m");
    propDrag("Rotate", &p.snapRotate, 1.0f, p.MIN_SNAP_ROTATE, p.MAX_SNAP_ROTATE, "%.0f deg");
    propDrag("Scale", &p.snapScale, 0.01f, p.MIN_SNAP_SCALE, p.MAX_SNAP_SCALE, "%.2f");

    ImGui::Spacing();
    ImGui::TextDisabled("The active tool and Local/World space are on the viewport's tool strip.");
}

void PreferencesPanel::drawDisplaySection(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    auto& window = ctx.window;
    ImGui::Text("Resolution: %zux%zu", window.getWidth(), window.getHeight());
    ImGui::Text("Worker threads: %zu", ThreadPool::get().threadCount());

    ImGui::Spacing();
    sectionLabel("Interface");
    // Multiplies the display's content scale: a large monitor at 100% still wants bigger text.
    // See docs/reference/editor.md.
    const char* uiScaleTooltip =
        "How much bigger the editor is than the display asks for. Follows you rather than the project.";
    propDrag(
        "UI Scale",
        &ec.state.prefs.uiScale,
        0.05f,
        Preferences::MIN_UI_SCALE,
        Preferences::MAX_UI_SCALE,
        "%.2fx",
        uiScaleTooltip
    );
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##uiscale")) ec.state.prefs.uiScale = 1.0f;

    // Applied by EditorSystem outside a play session, where a game may set the window up itself.
    Preferences& prefs = ec.state.prefs;
    ImGui::Spacing();
    sectionLabel("Window Mode");
    if (ImGui::RadioButton("Windowed", prefs.windowMode == WindowMode::Windowed))
        prefs.windowMode = WindowMode::Windowed;
    ImGui::SameLine(0, EditorStyle::px(16.0f));
    if (ImGui::RadioButton("Fullscreen", prefs.windowMode == WindowMode::Fullscreen))
        prefs.windowMode = WindowMode::Fullscreen;

    ImGui::Spacing();
    sectionLabel("VSync");
    propCheckbox("Sync", &prefs.vsync, "Sync to the display's refresh");

    ImGui::Spacing();
    sectionLabel("Frame Cap");
    // Taken on Enter, a step or leaving the field, not per keystroke: "120" typed would hold the
    // window to 1 and then 12 on the way.
    propRow("FPS Limit", "Frames per second the window is held to; 0 is unlimited", [&] {
        ImGui::SetNextItemWidth(EditorStyle::px(80.0f));
        const ImGuiInputTextFlags onEnter = ImGuiInputTextFlags_EnterReturnsTrue;
        const bool entered = ImGui::InputInt("##v", &m_fpsLimitEdit, 30, 100, onEnter);
        m_fpsLimitEdit = std::max(0, m_fpsLimitEdit);
        if (entered || ImGui::IsItemDeactivatedAfterEdit()) prefs.fpsCap = m_fpsLimitEdit;
        return entered;
    });
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_fpsLimitEdit == 0 ? "(unlimited)" : "");
}

void PreferencesPanel::drawKeybindsSection(EditorState& state) {
    // The other actions bound as @p b is, a line each; empty when none is.
    auto sharedWith = [&](const char* label, const KeyBind& b) {
        std::string others;
        if (b.key == ImGuiKey_None) return others;
        for (const KeybindEntry& e : KEYBINDS) {
            if (std::strcmp(e.label, label) == 0 || !(state.prefs.keybinds.*e.field == b)) continue;
            others += others.empty() ? "" : "\n";
            others += e.label;
        }
        return others;
    };

    auto drawKeybindRow = [&](const char* label, KeyBind& bind) {
        drawPropertyLabel(label);
        char keyLabel[48];
        getKeyBindLabel(bind, keyLabel, sizeof(keyLabel));

        char btnId[80];
        snprintf(
            btnId,
            sizeof(btnId),
            "%s##%s",
            (m_rebindTarget == label) ? "Press key..." : keyLabel,
            label
        );

        if (ImGui::Button(btnId, ImVec2(EditorStyle::px(120.0f), 0))) {
            m_rebindTarget = label;
        }

        if (const std::string others = sharedWith(label, bind); !others.empty()) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
            ImGui::TextUnformatted("(!)");
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also bound to:\n%s", others.c_str());
        }

        if (m_rebindTarget == label) {
            // A click cancels: Escape is bindable, and the Button that opened the row fires on release.
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                m_rebindTarget = nullptr;
                return;
            }

            // Keyboard only: past it come the gamepad and mouse, which would bind the next click.
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k) {
                auto candidate = static_cast<ImGuiKey>(k);
                const bool modifier = candidate == ImGuiKey_LeftCtrl || candidate == ImGuiKey_RightCtrl
                    || candidate == ImGuiKey_LeftShift || candidate == ImGuiKey_RightShift
                    || candidate == ImGuiKey_LeftAlt || candidate == ImGuiKey_RightAlt;
                if (modifier) continue;

                if (ImGui::IsKeyPressed(candidate)) {
                    const ImGuiIO& io = ImGui::GetIO();
                    bind.key  = candidate;
                    bind.mods = 0;
                    if (io.KeyCtrl)  bind.mods |= KEY_MOD_CTRL;
                    if (io.KeyShift) bind.mods |= KEY_MOD_SHIFT;
                    if (io.KeyAlt)   bind.mods |= KEY_MOD_ALT;
                    m_rebindTarget = nullptr;
                    break;
                }
            }
        }
    };

    const char* group = nullptr;
    for (const KeybindEntry& e : KEYBINDS) {
        if (group == nullptr || std::strcmp(group, e.group) != 0) {
            if (group != nullptr) ImGui::Spacing();
            sectionLabel(e.group);
            group = e.group;
        }
        drawKeybindRow(e.label, state.prefs.keybinds.*e.field);
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset Keybinds")) {
        state.prefs.keybinds = EditorKeybinds{};
        m_rebindTarget = nullptr;
    }
}

} // namespace Vkm::Engine
