#include "overlays/runtime_settings_overlay.h"

#include <cstddef>
#include <string_view>

#include <imgui.h>

#include "framework/editor_state.h"
#include "system/render/render_system.h"
#include "system/render/render_view.h"

namespace Engine {

void RuntimeSettingsOverlay::draw(EditorState& state, RenderSystem& renderSystem) {
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

        auto& env = renderSystem.getEnvironment();

        if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Strength",  &env.bloom.strength,  0.0f, 0.3f, "%.3f");
            ImGui::SliderFloat("Threshold", &env.bloom.threshold, 0.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("Knee",      &env.bloom.knee,      0.0f, 1.0f, "%.2f");
        }

        if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Auto Exposure", &env.exposure.autoExposure);
            ImGui::BeginDisabled(!env.exposure.autoExposure);
            ImGui::SliderFloat("Key",   &env.exposure.key,   0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("Speed", &env.exposure.speed, 0.1f,  10.0f, "%.2f");
            ImGui::EndDisabled();
        }
    }
    ImGui::End();

    if (!open) state.runtimeSettingsVisible = false;
}

} // namespace Engine
