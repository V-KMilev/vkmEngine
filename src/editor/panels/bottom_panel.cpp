#include "bottom_panel.h"
#include "../editor_common.h"

#include "system/camera/camera_controller.h"
#include "platform/window/window_manager.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "system/render/render_pipeline.h"
#include "platform/threading/thread_pool.h"

namespace Engine {

void BottomPanel::draw(FrameContext& ctx, EditorState& state) {
    if (!ImGui::BeginTabBar("##BottomTabs")) return;

    drawRenderingTab(ctx, state);
    drawEnvironmentTab(ctx);
    drawViewportTab(ctx, state);
    drawEditorTab(state);
    drawStatisticsTab(ctx);

    ImGui::EndTabBar();
}

// Rendering: wireframe, render passes, exposure, visibility culling
void BottomPanel::drawRenderingTab(FrameContext& ctx, EditorState& state) {
    if (!ImGui::BeginTabItem("Rendering")) return;
    ImGui::Spacing();

    ImGui::Checkbox("Wireframe", &state.wireframe);

    ImGui::Spacing();
    ImGui::SeparatorText("Render Passes");
    if (m_renderSystem) {
        auto& pipeline = m_renderSystem->getPipeline();
        for (size_t i = 0; i < pipeline.passCount(); ++i) {
            auto& pass = pipeline.getPass(i);
            bool enabled = pass.isEnabled();
            if (ImGui::Checkbox(pass.getName().c_str(), &enabled))
                pass.setEnabled(enabled);
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Exposure");
    {
        float exp = (ctx.visibility && ctx.visibility->hasCamera)
            ? ctx.visibility->cameraExposure : 1.0f;
        ImGui::TextDisabled("Camera exposure: %.2f (edit on Camera entity)", exp);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Visibility Culling");
    if (m_visibilitySystem) {
        auto& settings = m_visibilitySystem->getSettings();
        drawPropertyLabel("Min Pixels");
        ImGui::DragFloat("##MinPx", &settings.minPixels, 0.1f, 0.0f, 100.0f, "%.1f");

        drawPropertyLabel("Max Distance");
        ImGui::DragFloat("##MaxD", &settings.maxDistance, 1.0f, 10.0f, 10000.0f, "%.0f");

        if (ctx.visibility) {
            size_t vis = ctx.visibility->entries.size();
            size_t tot = ctx.scene.entityCount();
            ImGui::TextDisabled("Culled: %zu / %zu", tot > vis ? tot - vis : 0, tot);
        }
    }
    ImGui::EndTabItem();
}

// Environment: ambient light, clear color, grid, AABB debug
void BottomPanel::drawEnvironmentTab(FrameContext& ctx) {
    if (!ImGui::BeginTabItem("Environment")) return;
    ImGui::Spacing();

    if (m_renderSystem) {
        auto& env = m_renderSystem->getEnvironment();

        ImGui::SeparatorText("Ambient Light");
        drawPropertyLabel("Color");
        ImGui::ColorEdit3("##AmbCol", glm::value_ptr(env.ambientColor), ImGuiColorEditFlags_Float);
        drawPropertyLabel("Intensity");
        ImGui::DragFloat("##AmbInt", &env.ambientIntensity, 0.005f, 0.0f, 2.0f, "%.3f");

        ImGui::Spacing();
        ImGui::SeparatorText("Background");
        drawPropertyLabel("Clear Color");
        ImGui::ColorEdit3("##ClearCol", glm::value_ptr(env.clearColor), ImGuiColorEditFlags_Float);

        ImGui::Spacing();
        ImGui::SeparatorText("Grid");
        drawPropertyLabel("Cell Size");
        ImGui::DragFloat("##GScale", &env.gridScale, 0.1f, 0.1f, 100.0f, "%.1f");
        drawPropertyLabel("Grid Size");
        ImGui::DragFloat("##GSize", &env.gridSize, 10.0f, 10.0f, 10000.0f, "%.0f");
        drawPropertyLabel("Fade Start");
        ImGui::DragFloat("##GFadeS", &env.gridFadeStart, 1.0f, 1.0f, env.gridFadeEnd, "%.0f");
        drawPropertyLabel("Fade End");
        ImGui::DragFloat("##GFadeE", &env.gridFadeEnd, 1.0f, env.gridFadeStart, 10000.0f, "%.0f");

        ImGui::Spacing();
        ImGui::SeparatorText("AABB Debug");
        drawPropertyLabel("Color");
        ImGui::ColorEdit3("##AABBCol", glm::value_ptr(env.debugColor), ImGuiColorEditFlags_Float);
    }
    ImGui::EndTabItem();
}

// Viewport: camera controller + display settings (merged)
void BottomPanel::drawViewportTab(FrameContext& ctx, EditorState& state) {
    if (!ImGui::BeginTabItem("Viewport")) return;
    ImGui::Spacing();

    // Camera controls
    ImGui::SeparatorText("Camera");
    if (m_cameraController) {
        auto& s = m_cameraController->getSettings();
        drawPropertyLabel("Move Speed");   ImGui::DragFloat("##MS", &s.moveSpeed, 0.5f, 0.1f, 200.0f);
        drawPropertyLabel("Speed Boost");  ImGui::DragFloat("##SB", &s.speedBoost, 0.1f, 1.0f, 20.0f, "%.1fx");
        drawPropertyLabel("Look Sens.");   ImGui::DragFloat("##LS", &s.lookSensitivity, 0.0001f, 0.0001f, 0.01f, "%.4f");
        ImGui::Spacing();
        if (ImGui::Button("Reset Camera")) s = CameraController::Settings{};
    } else {
        ImGui::TextDisabled("No camera controller");
    }

    // Display settings
    auto& window = ctx.window;
    ImGui::Spacing();
    ImGui::SeparatorText("Display");
    ImGui::Text("Resolution: %zux%zu", window.getWidth(), window.getHeight());
    ImGui::Text("Threads: %zu", ThreadPool::get().threadCount());

    ImGui::Spacing();
    if (ImGui::Button("Fullscreen", ImVec2(100, 0))) window.updateMode(WindowMode::FULLSCREEN);
    ImGui::SameLine();
    if (ImGui::Button("Windowed", ImVec2(100, 0))) window.updateMode(WindowMode::WINDOWED);
    ImGui::SameLine(0, 16);
    if (ImGui::Button("VSync On", ImVec2(70, 0))) window.setVSync(true);
    ImGui::SameLine();
    if (ImGui::Button("VSync Off", ImVec2(70, 0))) window.setVSync(false);

    ImGui::Spacing();
    static int fpsLimit = 0;
    drawPropertyLabel("FPS Limit");
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("##FPSLim", &fpsLimit, 30);
    fpsLimit = std::max(0, fpsLimit);
    ImGui::SameLine();
    if (ImGui::Button("Apply##fps")) window.setFramerate(fpsLimit);
    ImGui::SameLine();
    ImGui::TextDisabled(fpsLimit == 0 ? "(unlimited)" : "");

    ImGui::EndTabItem();
}

// Editor: gizmo snap settings + keybind rebinding (merged)
void BottomPanel::drawEditorTab(EditorState& state) {
    if (!ImGui::BeginTabItem("Editor")) return;
    ImGui::Spacing();

    // Gizmo snap
    ImGui::SeparatorText("Gizmo Snapping");
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
    ImGui::SeparatorText("Gizmo Mode");
    drawPropertyLabel("Operation");
    const char* opNames[] = {"Translate", "Rotate", "Scale"};
    int opIdx = static_cast<int>(state.gizmoOperation);
    if (ImGui::Combo("##GizOp", &opIdx, opNames, IM_ARRAYSIZE(opNames)))
        state.gizmoOperation = static_cast<GizmoOperation>(opIdx);
    drawPropertyLabel("Space");
    const char* modeNames[] = {"Local", "World"};
    int modeIdx = static_cast<int>(state.gizmoMode);
    if (ImGui::Combo("##GizMode", &modeIdx, modeNames, IM_ARRAYSIZE(modeNames)))
        state.gizmoMode = static_cast<GizmoMode>(modeIdx);

    // Keybinds
    ImGui::Spacing();
    ImGui::SeparatorText("Keybinds");

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

    ImGui::TextDisabled("Panels");
    drawKeybindRow("Toggle Stats",     state.keybinds.toggleStats);
    drawKeybindRow("Toggle Hierarchy", state.keybinds.toggleHierarchy);
    drawKeybindRow("Toggle Inspector", state.keybinds.toggleInspector);
    drawKeybindRow("Toggle Bottom",    state.keybinds.toggleBottom);
    drawKeybindRow("Toggle Editor",    state.keybinds.toggleEditor);

    ImGui::Spacing();
    ImGui::TextDisabled("Entity");
    drawKeybindRow("Delete",         state.keybinds.deleteEntity);
    drawKeybindRow("Deselect",       state.keybinds.deselect);
    drawKeybindRow("Duplicate",      state.keybinds.duplicate);
    drawKeybindRow("Focus Selected", state.keybinds.focusSelected);

    ImGui::Spacing();
    ImGui::TextDisabled("Gizmo (disabled during fly-cam)");
    drawKeybindRow("Translate",   state.keybinds.gizmoTranslate);
    drawKeybindRow("Rotate",      state.keybinds.gizmoRotate);
    drawKeybindRow("Scale",       state.keybinds.gizmoScale);
    drawKeybindRow("Local/World", state.keybinds.gizmoToggleSpace);

    ImGui::Spacing();
    if (ImGui::Button("Reset Keybinds")) {
        state.keybinds = EditorKeybinds{};
        s_rebindTarget = nullptr;
    }

    ImGui::EndTabItem();
}

// Statistics: component counts, animation/light breakdown
void BottomPanel::drawStatisticsTab(FrameContext& ctx) {
    if (!ImGui::BeginTabItem("Statistics")) return;

    auto& scene = ctx.scene;

    // Update cached counts periodically (every 0.5s), not every frame
    m_resourceCounts.updateTimer += ctx.deltaTime;
    if (m_resourceCounts.updateTimer >= 0.5f) {
        m_resourceCounts.updateTimer = 0.0f;
        auto& rc = m_resourceCounts;
        rc.transforms  = scene.count<Transform>();
        rc.meshes      = scene.count<Mesh>();
        rc.lights      = scene.count<Light>();
        rc.cameras     = scene.count<Camera>();
        rc.animations  = scene.count<Animation>();
        rc.hierarchies = scene.count<Hierarchy>();
        rc.names       = scene.count<Name>();

        rc.animPlaying = rc.animPaused = 0;
        scene.forEach<Animation>([&](EntityId, const Animation& a) {
            if (a.playing) ++rc.animPlaying; else ++rc.animPaused;
        });

        rc.lightsDir = rc.lightsPoint = rc.lightsSpot = rc.lightsDisabled = 0;
        scene.forEach<Light>([&](EntityId, const Light& l) {
            if (!l.enabled) { ++rc.lightsDisabled; return; }
            switch (l.type) {
                case LightType::Directional: ++rc.lightsDir; break;
                case LightType::Point: ++rc.lightsPoint; break;
                case LightType::Spot: ++rc.lightsSpot; break;
            }
        });
    }

    const auto& rc = m_resourceCounts;
    float colW = ImGui::GetContentRegionAvail().x / 3.0f;

    ImGui::Columns(3, "##ResCols", true);
    ImGui::SetColumnWidth(0, colW);
    ImGui::SetColumnWidth(1, colW);

    ImGui::TextDisabled("Component Counts");
    ImGui::Separator();
    struct CI { const char* n; size_t c; };
    CI comps[] = {
        {"Transform", rc.transforms}, {"Mesh", rc.meshes},
        {"Light", rc.lights}, {"Camera", rc.cameras},
        {"Animation", rc.animations}, {"Hierarchy", rc.hierarchies},
        {"Name", rc.names},
    };
    for (const auto& co : comps) ImGui::Text("%-12s %zu", co.n, co.c);

    ImGui::NextColumn();

    ImGui::TextDisabled("Animations");
    ImGui::Separator();
    ImGui::Text("Playing: %u  Paused: %u", rc.animPlaying, rc.animPaused);
    if (ImGui::SmallButton("Pause All")) {
        scene.forEach<Animation>([](EntityId, Animation& a) { a.playing = false; });
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Resume All")) {
        scene.forEach<Animation>([](EntityId, Animation& a) { a.playing = true; });
    }

    ImGui::NextColumn();

    ImGui::TextDisabled("Lights");
    ImGui::Separator();
    ImGui::Text("Dir: %u  Point: %u  Spot: %u", rc.lightsDir, rc.lightsPoint, rc.lightsSpot);
    if (rc.lightsDisabled > 0) ImGui::Text("Disabled: %u", rc.lightsDisabled);

    ImGui::Columns(1);

    ImGui::EndTabItem();
}

} // namespace Engine
