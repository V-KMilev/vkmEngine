#include "panels/preferences_panel.h"

#include <algorithm>
#include <cstring>

#include "framework/editor_common.h"
#include "ui/editor_style.h"

#include "platform/threading/thread_pool.h"
#include "platform/window/window_manager.h"
#include "system/camera/camera_controller_system.h"

namespace Vkm::Engine {

void PreferencesPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Preferences", &state.showPreferences, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // Tab bar instead of master-detail: four sections aren't enough to
    // justify a sidebar.
    if (ImGui::BeginTabBar("##PrefTabs", ImGuiTabBarFlags_None)) {
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
            drawDisplaySection(ctx);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Keybinds")) {
            ImGui::Spacing();
            drawKeybindsSection(state);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void PreferencesPanel::drawCameraSection(EditorContext& ec) {
    auto& s = ec.cameraController.getSettings();
    propDrag("Move Speed", &s.moveSpeed, 0.5f, 0.1f, 200.0f);
    propDrag("Speed Boost", &s.speedBoost, 0.1f, 1.0f, 20.0f, "%.1fx");
    propDrag("Look Sens.", &s.lookSensitivity, 0.0001f, 0.0001f, 0.01f, "%.4f");
    propDrag("Zoom Sens.", &s.zoomSensitivity, 0.001f, 0.001f, 0.5f, "%.3f");
    propDrag("Scroll Mult.", &s.scrollMultiplier, 0.1f, 0.1f, 10.0f, "%.1f");
    propDrag("Min Pitch", &s.minPitch, 0.5f, -90.0f, 0.0f, "%.0f deg");
    propDrag("Max Pitch", &s.maxPitch, 0.5f, 0.0f, 90.0f, "%.0f deg");
    ImGui::Spacing();
    if (ImGui::Button("Reset to Defaults")) s = CameraControllerSystem::Settings{};
}

void PreferencesPanel::drawGizmoSection(EditorState& state) {
    ImGui::SeparatorText("Snapping");
    ImGui::Checkbox("Snap Enabled", &state.snapEnabled);
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("(Hold Ctrl to temporarily snap)");

    ImGui::Spacing();
    propDrag("Translate", &state.snapTranslate, 0.1f, 0.01f, 100.0f, "%.2f units");
    propDrag("Rotate", &state.snapRotate, 1.0f, 1.0f, 180.0f, "%.0f deg");
    propDrag("Scale", &state.snapScale, 0.01f, 0.01f, 10.0f, "%.2f");

    ImGui::Spacing();
    ImGui::TextDisabled("The active tool and Local/World space are on the viewport toolbar.");
}

void PreferencesPanel::drawDisplaySection(FrameContext& ctx) {
    auto& window = ctx.window;
    ImGui::Text("Resolution: %zux%zu", window.getWidth(), window.getHeight());
    ImGui::Text("Worker threads: %zu", ThreadPool::get().threadCount());

    ImGui::Spacing();
    ImGui::SeparatorText("Window Mode");
    // Radios, not buttons: they reflect which mode is actually applied.
    if (ImGui::RadioButton("Windowed", window.mode() == WindowMode::Windowed))
        window.updateMode(WindowMode::Windowed);
    ImGui::SameLine(0, EditorStyle::px(16.0f));
    if (ImGui::RadioButton("Fullscreen", window.mode() == WindowMode::Fullscreen))
        window.updateMode(WindowMode::Fullscreen);

    ImGui::Spacing();
    ImGui::SeparatorText("VSync");
    bool vsync = window.vsync();
    if (ImGui::Checkbox("Sync to display refresh", &vsync)) window.setVSync(vsync);

    ImGui::Spacing();
    ImGui::SeparatorText("Frame Cap");
    drawPropertyLabel("FPS Limit");
    ImGui::SetNextItemWidth(EditorStyle::px(80.0f));
    ImGui::InputInt("##FPSLim", &m_fpsLimitEdit, 30);
    m_fpsLimitEdit = std::max(0, m_fpsLimitEdit);
    ImGui::SameLine();
    if (ImGui::Button("Apply##fps")) window.setFramerate(m_fpsLimitEdit);
    ImGui::SameLine();
    ImGui::TextDisabled(m_fpsLimitEdit == 0 ? "(unlimited)" : "");
}

void PreferencesPanel::drawKeybindsSection(EditorState& state) {
    auto isConflict = [&](const KeyBind& b) {
        if (b.key == ImGuiKey_None) return false;
        int n = 0;
        for (const KeybindEntry& e : KEYBINDS) if (state.keybinds.*e.field == b) ++n;
        return n > 1;
    };

    auto drawKeybindRow = [&](const char* label, KeyBind& bind) {
        drawPropertyLabel(label);
        char keyLabel[48];
        getKeyBindLabel(bind, keyLabel, sizeof(keyLabel));

        char btnId[80];
        snprintf(btnId, sizeof(btnId), "%s##%s",
                 (m_rebindTarget == label) ? "Press key..." : keyLabel, label);

        if (ImGui::Button(btnId, ImVec2(EditorStyle::px(120.0f), 0))) {
            m_rebindTarget = label;
        }

        if (isConflict(bind)) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
            ImGui::TextUnformatted("(!)");
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("This shortcut is also bound to another action.");
        }

        if (m_rebindTarget == label) {
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                auto candidate = static_cast<ImGuiKey>(k);
                if (candidate == ImGuiKey_LeftCtrl  || candidate == ImGuiKey_RightCtrl  ||
                    candidate == ImGuiKey_LeftShift || candidate == ImGuiKey_RightShift ||
                    candidate == ImGuiKey_LeftAlt   || candidate == ImGuiKey_RightAlt)
                    continue;

                if (ImGui::IsKeyPressed(candidate)) {
                    const ImGuiIO& io = ImGui::GetIO();
                    bind.key  = candidate;
                    bind.mods = 0;
                    if (io.KeyCtrl)  bind.mods |= KeyMod_Ctrl;
                    if (io.KeyShift) bind.mods |= KeyMod_Shift;
                    if (io.KeyAlt)   bind.mods |= KeyMod_Alt;
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
        drawKeybindRow(e.label, state.keybinds.*e.field);
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset Keybinds")) {
        state.keybinds = EditorKeybinds{};
        m_rebindTarget = nullptr;
    }
}

} // namespace Vkm::Engine
