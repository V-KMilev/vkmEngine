#include "panels/preferences_panel.h"
#include "framework/editor_common.h"
#include "ui/editor_style.h"

#include "system/camera/camera_controller.h"
#include "platform/window/window_manager.h"
#include "platform/threading/thread_pool.h"

namespace Engine {

namespace {
    struct SectionDef { const char* group; const char* name; const char* hint; };

    // Order matches the dispatch switch in PreferencesPanel::draw().
    const SectionDef kSections[] = {
        {"VIEWPORT",    "Camera",   "Fly-camera movement and sensitivity"},
        {"VIEWPORT",    "Gizmo",    "Transform snap step sizes"},
        {"APPLICATION", "Display",  "Resolution, fullscreen, VSync, FPS cap"},
        {"INPUT",       "Keybinds", "Rebind editor shortcuts"},
    };
    constexpr int kSectionCount = static_cast<int>(sizeof(kSections) / sizeof(kSections[0]));

    void sectionHeader(const char* title, const char* hint) {
        drawSectionHeader(title, hint);
    }
}

void PreferencesPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(660, 460), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Preferences", &state.showPreferences, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::NAV_BG);
    if (ImGui::BeginChild("##PrefNav", ImVec2(150.0f, avail.y), ImGuiChildFlags_Borders)) {
        const char* lastGroup = nullptr;
        for (int i = 0; i < kSectionCount; ++i) {
            if (kSections[i].group != lastGroup) {
                if (lastGroup) ImGui::Spacing();
                ImGui::TextDisabled("%s", kSections[i].group);
                lastGroup = kSections[i].group;
            }
            ImGui::Indent(8.0f);
            if (ImGui::Selectable(kSections[i].name, m_selectedSection == i))
                m_selectedSection = i;
            ImGui::Unindent(8.0f);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 6);

    if (ImGui::BeginChild("##PrefDetail", ImVec2(0, avail.y), ImGuiChildFlags_Borders)) {
        const auto& s = kSections[m_selectedSection];
        sectionHeader(s.name, s.hint);
        switch (m_selectedSection) {
            case 0: drawCameraSection(ec);        break;
            case 1: drawGizmoSection(state);      break;
            case 2: drawDisplaySection(ctx);      break;
            case 3: drawKeybindsSection(state);   break;
            default: break;
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void PreferencesPanel::drawCameraSection(EditorContext& ec) {
    if (!ec.cameraController) {
        ImGui::TextDisabled("No camera controller");
        return;
    }
    auto& s = ec.cameraController->getSettings();
    drawPropertyLabel("Move Speed");   ImGui::DragFloat("##MS", &s.moveSpeed, 0.5f, 0.1f, 200.0f);
    drawPropertyLabel("Speed Boost");  ImGui::DragFloat("##SB", &s.speedBoost, 0.1f, 1.0f, 20.0f, "%.1fx");
    drawPropertyLabel("Look Sens.");   ImGui::DragFloat("##LS", &s.lookSensitivity, 0.0001f, 0.0001f, 0.01f, "%.4f");
    drawPropertyLabel("Zoom Sens.");   ImGui::DragFloat("##ZS", &s.zoomSensitivity, 0.001f, 0.001f, 0.5f, "%.3f");
    drawPropertyLabel("Scroll Mult.");  ImGui::DragFloat("##SM", &s.scrollMultiplier, 0.1f, 0.1f, 10.0f, "%.1f");
    drawPropertyLabel("Min Pitch");    ImGui::DragFloat("##MnP", &s.minPitch, 0.5f, -90.0f, 0.0f, "%.0f deg");
    drawPropertyLabel("Max Pitch");    ImGui::DragFloat("##MxP", &s.maxPitch, 0.5f, 0.0f, 90.0f, "%.0f deg");
    ImGui::Spacing();
    if (ImGui::Button("Reset to Defaults")) s = CameraController::Settings{};
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
    if (ImGui::Button("Fullscreen", ImVec2(110, 0))) window.updateMode(WindowMode::FULLSCREEN);
    ImGui::SameLine();
    if (ImGui::Button("Windowed", ImVec2(110, 0))) window.updateMode(WindowMode::WINDOWED);

    ImGui::Spacing();
    ImGui::SeparatorText("VSync");
    if (ImGui::Button("VSync On", ImVec2(110, 0))) window.setVSync(true);
    ImGui::SameLine();
    if (ImGui::Button("VSync Off", ImVec2(110, 0))) window.setVSync(false);

    ImGui::Spacing();
    ImGui::SeparatorText("Frame Cap");
    static int fpsLimit = 0;
    drawPropertyLabel("FPS Limit");
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("##FPSLim", &fpsLimit, 30);
    fpsLimit = std::max(0, fpsLimit);
    ImGui::SameLine();
    if (ImGui::Button("Apply##fps")) window.setFramerate(fpsLimit);
    ImGui::SameLine();
    ImGui::TextDisabled(fpsLimit == 0 ? "(unlimited)" : "");
}

void PreferencesPanel::drawKeybindsSection(EditorState& state) {
    static const char* s_rebindTarget = nullptr;

    auto drawKeybindRow = [&](const char* label, KeyBind& bind) {
        drawPropertyLabel(label);
        char keyLabel[48];
        getKeyBindLabel(bind, keyLabel, sizeof(keyLabel));

        char btnId[80];
        snprintf(btnId, sizeof(btnId), "%s##%s",
                 (s_rebindTarget == label) ? "Press key..." : keyLabel, label);

        if (ImGui::Button(btnId, ImVec2(120, 0))) {
            s_rebindTarget = label;
        }

        if (s_rebindTarget == label) {
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
                    s_rebindTarget = nullptr;
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
    ImGui::TextDisabled("Windows & Panels");
    drawKeybindRow("Toggle Stats",     state.keybinds.toggleStats);
    drawKeybindRow("Toggle Hierarchy", state.keybinds.toggleHierarchy);
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
        s_rebindTarget = nullptr;
    }
}

} // namespace Engine
