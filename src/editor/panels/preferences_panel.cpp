#include "panels/preferences_panel.h"

#include <algorithm>
#include <cstring>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/window_names.h"
#include "core/system.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_widgets.h"
#include "ui/style/editor_style.h"
#include "ui/rows.h"

#include "vkm_platform/threading/thread_pool.h"
#include "vkm_platform/window/window_manager.h"
#include "view/camera_controller_system.h"

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
    Rows<Settings> rows(ec.state.prefs.camera, {}, nullptr);
    rows.row<&Settings::moveSpeed>("%.1f m/s");
    rows.row<&Settings::speedBoost>("%.1fx");
    rows.row<&Settings::lookSensitivity>("%.4f");
    rows.row<&Settings::dollyStep>("%.2f");
    rows.row<&Settings::minPitch>("%.0f deg");
    rows.row<&Settings::maxPitch>("%.0f deg");
    ImGui::Spacing();
    if (ImGui::Button("Reset to Defaults")) ec.state.prefs.camera = Settings{};
}

void PreferencesPanel::drawGizmoSection(EditorState& state) {
    Rows<Preferences> rows(state.prefs, {}, nullptr);
    rows.row<&Preferences::snapEnabled>();
    rows.row<&Preferences::snapTranslate>("%.2f m");
    rows.row<&Preferences::snapRotate>("%.0f deg");
    rows.row<&Preferences::snapScale>("%.2f");
    ImGui::Spacing();
    cardNote(Note::Info, "The tool and its Local or World space are on the viewport's tool strip.");
}

void PreferencesPanel::drawDisplaySection(EditorContext& ec) {
    Platform::WindowManager& window = ec.frame.window;
    ImGui::Text("Resolution: %zux%zu", window.getWidth(), window.getHeight());
    ImGui::Text("Worker threads: %zu", Platform::ThreadPool::get().threadCount());
    ImGui::Spacing();

    // Applied by EditorSystem outside a play session, where a game may set the window up itself.
    Rows<Preferences> rows(ec.state.prefs, {}, nullptr);
    rows.row<&Preferences::uiScale>("%.2fx");
    rows.row<&Preferences::windowMode>();
    rows.row<&Preferences::vsync>();
    // A step taken at once, typed text once the field lets go - on Enter or leaving it - not per
    // keystroke: "120" typed would hold the window to 1 and then 12 on the way. InputInt takes no
    // EnterReturnsTrue (ImGui asserts it), so whether the text field was the one edited is asked:
    // InputScalar names it by the label, as it pushes the label and takes "" inside.
    rows.rowWith<&Preferences::fpsCap>([&](int& cap, const FieldInfo& info) {
        ImGui::SetNextItemWidth(EditorStyle::px(80.0f));
        const ImGuiID field   = ImGui::GetID("##v");
        const bool    changed = ImGui::InputInt("##v", &m_fpsLimitEdit, 30, 100);
        m_fpsLimitEdit = std::clamp(m_fpsLimitEdit, static_cast<int>(info.min), static_cast<int>(info.max));
        const bool taken = (changed && ImGui::GetActiveID() != field) || ImGui::IsItemDeactivatedAfterEdit();
        if (taken) cap = m_fpsLimitEdit;
        return taken;
    });
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
        m_rebindTarget       = nullptr;
    }
}

} // namespace Vkm::Engine
