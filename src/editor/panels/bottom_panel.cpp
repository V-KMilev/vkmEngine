#include "../editor_common.h"

#include "core/engine.h"
#include "camera_controller.h"
#include "visibility/visibility_system.h"
#include "render/render_system.h"
#include "render/render_pipeline.h"
#include "platform/threading/thread_pool.h"

namespace Engine {
void EditorSystem::drawBottomPanel(FrameContext& ctx) {
    if (ImGui::BeginTabBar("##BottomTabs")) {
        if (ImGui::BeginTabItem("Settings")) {
            drawSettingsTab(ctx);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Resources")) {
            drawResourcesTab(ctx);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void EditorSystem::drawSettingsTab(FrameContext& ctx) {
    if (ImGui::BeginTabBar("##SettingsTabs")) {
        // Rendering
        if (ImGui::BeginTabItem("Rendering")) {
            ImGui::Spacing();

            ImGui::Checkbox("Wireframe", &m_wireframe);

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
            ImGui::SeparatorText("Visibility Culling");
            if (m_visibilitySystem) {
                float minPx = m_visibilitySystem->getMinPixels();
                drawPropertyLabel("Min Pixels");
                if (ImGui::DragFloat("##MinPx", &minPx, 0.1f, 0.0f, 100.0f, "%.1f"))
                    m_visibilitySystem->setMinPixels(minPx);

                float maxDist = m_visibilitySystem->getMaxDistance();
                drawPropertyLabel("Max Distance");
                if (ImGui::DragFloat("##MaxD", &maxDist, 1.0f, 10.0f, 10000.0f, "%.0f"))
                    m_visibilitySystem->setMaxDistance(maxDist);

                if (ctx.visibility) {
                    size_t vis = ctx.visibility->entries.size();
                    size_t tot = ctx.scene.entityCount();
                    ImGui::TextDisabled("Culled: %zu / %zu", tot > vis ? tot - vis : 0, tot);
                }
            }
            ImGui::EndTabItem();
        }

        // Camera
        if (ImGui::BeginTabItem("Camera")) {
            ImGui::Spacing();
            if (m_cameraController) {
                auto& s = m_cameraController->getSettings();
                drawPropertyLabel("Move Speed");   ImGui::DragFloat("##MS", &s.moveSpeed, 0.5f, 0.1f, 200.0f);
                drawPropertyLabel("Speed Boost");  ImGui::DragFloat("##SB", &s.speedBoost, 0.1f, 1.0f, 20.0f, "%.1fx");
                drawPropertyLabel("Look Sens.");   ImGui::DragFloat("##LS", &s.lookSensitivity, 0.0001f, 0.0001f, 0.01f, "%.4f");
                ImGui::Spacing();
                if (ImGui::Button("Reset to Defaults")) s = CameraControllerSettings{};
            } else {
                ImGui::TextDisabled("No camera controller");
            }
            ImGui::EndTabItem();
        }

        // Display
        if (ImGui::BeginTabItem("Display")) {
            auto& window = Engine::get().getWindow();
            ImGui::Spacing();

            ImGui::Text("Resolution: %zux%zu", window.getWidth(), window.getHeight());
            ImGui::Text("Threads: %zu", ThreadPool::get().size());

            ImGui::Spacing();
            ImGui::SeparatorText("VSync");
            if (ImGui::Button("On##VS", ImVec2(60, 0))) window.setVSync(true);
            ImGui::SameLine();
            if (ImGui::Button("Off##VS", ImVec2(60, 0))) window.setVSync(false);

            ImGui::Spacing();
            ImGui::SeparatorText("Frame Rate Limit");
            static int fpsLimit = 0;
            ImGui::SetNextItemWidth(80);
            ImGui::InputInt("##FPSLim", &fpsLimit, 30);
            fpsLimit = std::max(0, fpsLimit);
            ImGui::SameLine();
            if (ImGui::Button("Apply##fps")) window.setFramerate(fpsLimit);
            ImGui::SameLine();
            ImGui::TextDisabled(fpsLimit == 0 ? "(unlimited)" : "");

            ImGui::EndTabItem();
        }

        // Keybinds
        if (ImGui::BeginTabItem("Keybinds")) {
            ImGui::Spacing();

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

            ImGui::SeparatorText("Panels");
            drawKeybindRow("Toggle Stats",     m_keybinds.toggleStats);
            drawKeybindRow("Toggle Hierarchy", m_keybinds.toggleHierarchy);
            drawKeybindRow("Toggle Inspector", m_keybinds.toggleInspector);
            drawKeybindRow("Toggle Bottom",    m_keybinds.toggleBottom);
            drawKeybindRow("Toggle Editor",    m_keybinds.toggleEditor);

            ImGui::SeparatorText("Entity");
            drawKeybindRow("Delete",         m_keybinds.deleteEntity);
            drawKeybindRow("Deselect",       m_keybinds.deselect);
            drawKeybindRow("Duplicate",      m_keybinds.duplicate);
            drawKeybindRow("Focus Selected", m_keybinds.focusSelected);

            ImGui::SeparatorText("Gizmo (disabled during fly-cam)");
            drawKeybindRow("Translate",   m_keybinds.gizmoTranslate);
            drawKeybindRow("Rotate",      m_keybinds.gizmoRotate);
            drawKeybindRow("Scale",       m_keybinds.gizmoScale);
            drawKeybindRow("Local/World", m_keybinds.gizmoToggleSpace);

            ImGui::Spacing();
            if (ImGui::Button("Reset to Defaults")) {
                m_keybinds = EditorKeybinds{};
                s_rebindTarget = nullptr;
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

void EditorSystem::drawResourcesTab(FrameContext& ctx) {
    auto& scene = ctx.scene;
    float colW = ImGui::GetContentRegionAvail().x / 3.0f;

    ImGui::Columns(3, "##ResCols", true);
    ImGui::SetColumnWidth(0, colW);
    ImGui::SetColumnWidth(1, colW);

    // Column 1: Components
    ImGui::TextDisabled("Component Counts");
    ImGui::Separator();
    struct CI { const char* n; size_t c; };
    CI comps[] = {
        {"Transform", scene.count<Transform>()}, {"Mesh", scene.count<Mesh>()},
        {"Light", scene.count<Light>()}, {"Camera", scene.count<Camera>()},
        {"Animation", scene.count<Animation>()}, {"Hierarchy", scene.count<Hierarchy>()},
        {"Name", scene.count<Name>()},
    };
    for (const auto& co : comps) ImGui::Text("%-12s %zu", co.n, co.c);

    ImGui::NextColumn();

    // Column 2: Animations
    ImGui::TextDisabled("Animations");
    ImGui::Separator();
    uint32_t playing = 0, paused = 0;
    scene.forEach<Animation>([&](EntityId, const Animation& a) {
        if (a.playing) ++playing; else ++paused;
    });
    ImGui::Text("Playing: %u  Paused: %u", playing, paused);
    if (ImGui::SmallButton("Pause All")) {
        scene.forEach<Animation>([](EntityId, Animation& a) { a.playing = false; });
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Resume All")) {
        scene.forEach<Animation>([](EntityId, Animation& a) { a.playing = true; });
    }

    ImGui::NextColumn();

    // Column 3: Lights
    ImGui::TextDisabled("Lights");
    ImGui::Separator();
    uint32_t dir = 0, pt = 0, sp = 0, dis = 0;
    scene.forEach<Light>([&](EntityId, const Light& l) {
        if (!l.enabled) { ++dis; return; }
        switch (l.type) {
            case LightType::Directional: ++dir; break;
            case LightType::Point: ++pt; break;
            case LightType::Spot: ++sp; break;
        }
    });
    ImGui::Text("Dir: %u  Point: %u  Spot: %u", dir, pt, sp);
    if (dis > 0) ImGui::Text("Disabled: %u", dis);

    ImGui::Columns(1);
}

} // namespace Engine
