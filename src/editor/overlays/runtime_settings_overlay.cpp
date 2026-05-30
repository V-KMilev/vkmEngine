#include "overlays/runtime_settings_overlay.h"

#include <cstddef>
#include <string_view>

#include <imgui.h>

#include "ecs/scene.h"
#include "framework/editor_state.h"
#include "system/render/render_system.h"
#include "system/render/render_view.h"

namespace Engine {

void RuntimeSettingsOverlay::draw(EditorState& state, Scene& scene, RenderSystem& renderSystem) {
    if (!state.runtimeSettingsVisible) return;

    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    bool open = true;
    if (ImGui::Begin("Graphics Settings", &open,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {

        ImGui::TextDisabled("F10 to toggle");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Render Passes", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (std::size_t i = 0; i < renderSystem.passCount(); ++i) {
                bool enabled = renderSystem.isPassEnabled(i);
                const std::string_view name = renderSystem.passName(i);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Checkbox(name.data(), &enabled)) {
                    renderSystem.setPassEnabled(i, enabled);
                }
                ImGui::PopID();
            }
        }

        // The scene's Environment component is the source of truth: RenderSystem
        // copies it into its own mirror every frame, so editing the mirror
        // (renderSystem.getEnvironment()) would be overwritten next frame and
        // have no lasting effect. Mirrors the viewport toolbar / inspector.
        EnvironmentConfig* envPtr = nullptr;
        scene.forEach<EnvironmentConfig>(
            [&](EntityId, EnvironmentConfig& e) { if (!envPtr) envPtr = &e; });

        if (!envPtr) {
            ImGui::Separator();
            ImGui::TextDisabled("No Environment entity in the scene.");
        } else {
            EnvironmentConfig& env = *envPtr;
            bool changed = false;

            if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::SliderFloat("Strength",  &env.bloom.strength,  0.0f, 0.3f, "%.3f");
                changed |= ImGui::SliderFloat("Threshold", &env.bloom.threshold, 0.0f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Knee",      &env.bloom.knee,      0.0f, 1.0f, "%.2f");
            }

            if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::Checkbox("Auto Exposure", &env.exposure.autoExposure);
                ImGui::BeginDisabled(!env.exposure.autoExposure);
                changed |= ImGui::SliderFloat("Key",      &env.exposure.key,           0.05f, 1.0f,  "%.2f");
                changed |= ImGui::SliderFloat("Brighten", &env.exposure.speedBrighten, 0.1f,  10.0f, "%.2f");
                changed |= ImGui::SliderFloat("Darken",   &env.exposure.speedDarken,   0.1f,  10.0f, "%.2f");
                ImGui::EndDisabled();
            }

            if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
                // Match the inspector's resolution combo - common values only,
                // free-form input would just invite GL_OUT_OF_MEMORY on a typo.
                static const std::uint32_t kRes2D[]  = { 512, 1024, 2048, 4096, 8192 };
                static const char*  k2DNames        = "512\0" "1024\0" "2048\0" "4096\0" "8192\0";
                static const std::uint32_t kResCube[] = { 128, 256, 512, 1024, 2048 };
                static const char*  kCubeNames       = "128\0" "256\0" "512\0" "1024\0" "2048\0";

                auto pickIndex = [&](std::uint32_t current, const std::uint32_t* arr, int count) {
                    int idx = 0;
                    int bestDelta = (current > arr[0])
                        ? static_cast<int>(current - arr[0]) : static_cast<int>(arr[0] - current);
                    for (int i = 1; i < count; ++i) {
                        int d = (current > arr[i])
                            ? static_cast<int>(current - arr[i]) : static_cast<int>(arr[i] - current);
                        if (d < bestDelta) { bestDelta = d; idx = i; }
                    }
                    return idx;
                };

                int idx2D = pickIndex(env.shadow.atlasRes2D, kRes2D, 5);
                if (ImGui::Combo("Atlas 2D", &idx2D, k2DNames)) {
                    env.shadow.atlasRes2D = kRes2D[idx2D];
                    changed = true;
                }
                int idxCube = pickIndex(env.shadow.atlasResCube, kResCube, 5);
                if (ImGui::Combo("Atlas Cube", &idxCube, kCubeNames)) {
                    env.shadow.atlasResCube = kResCube[idxCube];
                    changed = true;
                }
                changed |= ImGui::SliderFloat("Softness", &env.shadow.softness, 0.0f, 1.0f, "%.2f");
            }

            if (ImGui::CollapsingHeader("Transparency")) {
                changed |= ImGui::Checkbox("Weighted-Blended OIT", &env.transparency.useOIT);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("On: correct for intersecting transparents.\n"
                                      "Off: sorted alpha-blend, supports refraction.");
                }
            }

            if (changed) state.markSceneDirty();
        }
    }
    ImGui::End();

    if (!open) state.runtimeSettingsVisible = false;
}

} // namespace Engine
