#include "panels/preferences_panel.h"

#include "framework/editor_common.h"
#include "ui/editor_style.h"

#include "platform/threading/thread_pool.h"
#include "platform/window/window_manager.h"
#include "system/camera/camera_controller_system.h"

namespace Engine {

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
    // justify a sidebar. Tabs are more idiomatic for a Preferences window.
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
    drawPropertyLabel("Move Speed");   ImGui::DragFloat("##MS", &s.moveSpeed, 0.5f, 0.1f, 200.0f);
    drawPropertyLabel("Speed Boost");  ImGui::DragFloat("##SB", &s.speedBoost, 0.1f, 1.0f, 20.0f, "%.1fx");
    drawPropertyLabel("Look Sens.");   ImGui::DragFloat("##LS", &s.lookSensitivity, 0.0001f, 0.0001f, 0.01f, "%.4f");
    drawPropertyLabel("Zoom Sens.");   ImGui::DragFloat("##ZS", &s.zoomSensitivity, 0.001f, 0.001f, 0.5f, "%.3f");
    drawPropertyLabel("Scroll Mult.");  ImGui::DragFloat("##SM", &s.scrollMultiplier, 0.1f, 0.1f, 10.0f, "%.1f");
    drawPropertyLabel("Min Pitch");    ImGui::DragFloat("##MnP", &s.minPitch, 0.5f, -90.0f, 0.0f, "%.0f deg");
    drawPropertyLabel("Max Pitch");    ImGui::DragFloat("##MxP", &s.maxPitch, 0.5f, 0.0f, 90.0f, "%.0f deg");
    ImGui::Spacing();
    if (ImGui::Button("Reset to Defaults")) s = CameraControllerSystem::Settings{};
}

void PreferencesPanel::drawGizmoSection(EditorState& state) {
    ImGui::SeparatorText("Snapping");
    ImGui::Checkbox("Snap Enabled", &state.snapEnabled);
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("(Hold Ctrl to temporarily snap)");

    ImGui::Spacing();
    drawPropertyLabel("Translate");
    ImGui::DragFloat("##SnapT", &state.snapTranslate, 0.1f, 0.01f, 100.0f, "%.2f units");
    drawPropertyLabel("Rotate");
    ImGui::DragFloat("##SnapR", &state.snapRotate, 1.0f, 1.0f, 180.0f, "%.0f deg");
    drawPropertyLabel("Scale");
    ImGui::DragFloat("##SnapS", &state.snapScale, 0.01f, 0.01f, 10.0f, "%.2f");

    ImGui::Spacing();
    ImGui::TextDisabled("The active tool and Local/World space are on the viewport toolbar.");
}

void PreferencesPanel::drawDisplaySection(FrameContext& ctx) {
    auto& window = ctx.window;
    ImGui::Text("Resolution: %zux%zu", window.getWidth(), window.getHeight());
    ImGui::Text("Worker threads: %zu", ThreadPool::get().threadCount());

    ImGui::Spacing();
    ImGui::SeparatorText("Window Mode");
    if (ImGui::Button("Fullscreen", ImVec2(110, 0))) window.updateMode(WindowMode::Fullscreen);
    ImGui::SameLine();
    if (ImGui::Button("Windowed", ImVec2(110, 0))) window.updateMode(WindowMode::Windowed);

    ImGui::Spacing();
    ImGui::SeparatorText("VSync");
    if (ImGui::Button("VSync On", ImVec2(110, 0))) window.setVSync(true);
    ImGui::SameLine();
    if (ImGui::Button("VSync Off", ImVec2(110, 0))) window.setVSync(false);

    ImGui::Spacing();
    ImGui::SeparatorText("Frame Cap");
    drawPropertyLabel("FPS Limit");
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("##FPSLim", &m_fpsLimitEdit, 30);
    m_fpsLimitEdit = std::max(0, m_fpsLimitEdit);
    ImGui::SameLine();
    if (ImGui::Button("Apply##fps")) window.setFramerate(m_fpsLimitEdit);
    ImGui::SameLine();
    ImGui::TextDisabled(m_fpsLimitEdit == 0 ? "(unlimited)" : "");
}

void PreferencesPanel::drawKeybindsSection(EditorState& state) {
    // Build a small frequency table of all current bindings so each row
    // can flag itself as a conflict in O(1). Manually enumerated rather
    // than iterating the struct - there's no introspection but the list
    // is short and stable.
    const KeyBind* all[] = {
        &state.keybinds.saveScene, &state.keybinds.saveSceneAs, &state.keybinds.loadScene,
        &state.keybinds.undo, &state.keybinds.redo,
        &state.keybinds.toggleHierarchy,
        &state.keybinds.toggleInspector, &state.keybinds.toggleBottom,
        &state.keybinds.toggleEditor,
        &state.keybinds.openPreferences,
        &state.keybinds.deleteEntity, &state.keybinds.deselect,
        &state.keybinds.duplicate, &state.keybinds.focusSelected,
        &state.keybinds.gizmoSelect, &state.keybinds.gizmoTranslate,
        &state.keybinds.gizmoRotate, &state.keybinds.gizmoScale,
        &state.keybinds.gizmoToggleSpace,
    };
    auto isConflict = [&](const KeyBind& b) {
        if (b.key == ImGuiKey_None) return false;
        int n = 0;
        for (const KeyBind* o : all) if (*o == b) ++n;
        return n > 1;
    };

    auto drawKeybindRow = [&](const char* label, KeyBind& bind) {
        drawPropertyLabel(label);
        char keyLabel[48];
        getKeyBindLabel(bind, keyLabel, sizeof(keyLabel));

        char btnId[80];
        snprintf(btnId, sizeof(btnId), "%s##%s",
                 (m_rebindTarget == label) ? "Press key..." : keyLabel, label);

        if (ImGui::Button(btnId, ImVec2(120, 0))) {
            m_rebindTarget = label;
        }

        if (isConflict(bind)) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.25f, 1.0f));
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

    ImGui::TextDisabled("File");
    drawKeybindRow("Save Scene",     state.keybinds.saveScene);
    drawKeybindRow("Save Scene As",  state.keybinds.saveSceneAs);
    drawKeybindRow("Load Scene",     state.keybinds.loadScene);

    ImGui::Spacing();
    ImGui::TextDisabled("Edit");
    drawKeybindRow("Undo",           state.keybinds.undo);
    drawKeybindRow("Redo",           state.keybinds.redo);

    ImGui::Spacing();
    ImGui::TextDisabled("Windows & Panels");
    drawKeybindRow("Toggle Scene",     state.keybinds.toggleHierarchy);
    drawKeybindRow("Toggle Inspector", state.keybinds.toggleInspector);
    drawKeybindRow("Toggle Bottom",    state.keybinds.toggleBottom);
    drawKeybindRow("Toggle Editor",    state.keybinds.toggleEditor);
    drawKeybindRow("Preferences",      state.keybinds.openPreferences);

    ImGui::Spacing();
    ImGui::TextDisabled("Entity");
    drawKeybindRow("Delete",         state.keybinds.deleteEntity);
    drawKeybindRow("Deselect",       state.keybinds.deselect);
    drawKeybindRow("Duplicate",      state.keybinds.duplicate);
    drawKeybindRow("Focus Selected", state.keybinds.focusSelected);

    ImGui::Spacing();
    ImGui::TextDisabled("Gizmo (disabled during fly-cam)");
    drawKeybindRow("Select",      state.keybinds.gizmoSelect);
    drawKeybindRow("Translate",   state.keybinds.gizmoTranslate);
    drawKeybindRow("Rotate",      state.keybinds.gizmoRotate);
    drawKeybindRow("Scale",       state.keybinds.gizmoScale);
    drawKeybindRow("Local/World", state.keybinds.gizmoToggleSpace);

    ImGui::Spacing();
    if (ImGui::Button("Reset Keybinds")) {
        state.keybinds = EditorKeybinds{};
        m_rebindTarget = nullptr;
    }
}

} // namespace Engine
