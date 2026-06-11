#include "panels/render_settings_panel.h"

#include "framework/editor_common.h"
#include "framework/editor_context.h"

#include "system/render/render_system.h"

namespace Engine {

void RenderSettingsPanel::draw(EditorContext& ec) {
    EditorState& state = ec.state;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 480), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Render Settings", &state.showRenderSettings, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    RenderSettings& s = ec.renderSystem.settings();

    if (ImGui::CollapsingHeader("Ambient Occlusion (GTAO)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##gtao", &s.gtao);
        ImGui::BeginDisabled(!s.gtao);
        drawPropertyLabel("Radius");    ImGui::DragFloat("##gtaoR", &s.gtaoRadius, 0.01f, 0.05f, 5.0f, "%.2f");
        drawPropertyLabel("Intensity"); ImGui::SliderFloat("##gtaoI", &s.gtaoIntensity, 0.0f, 3.0f, "%.2f");
        drawPropertyLabel("Power");     ImGui::SliderFloat("##gtaoP", &s.gtaoPower, 0.5f, 4.0f, "%.2f");
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Screen-Space Reflections", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##ssr", &s.ssr);
        ImGui::BeginDisabled(!s.ssr);
        drawPropertyLabel("Intensity");    ImGui::SliderFloat("##ssrI", &s.ssrIntensity, 0.0f, 2.0f, "%.2f");
        drawPropertyLabel("Max Distance"); ImGui::DragFloat("##ssrD", &s.ssrMaxDistance, 0.5f, 1.0f, 200.0f, "%.0f");
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Motion Blur", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##mb", &s.motionBlur);
        ImGui::BeginDisabled(!s.motionBlur);
        drawPropertyLabel("Intensity"); ImGui::SliderFloat("##mbI", &s.motionBlurIntensity, 0.0f, 3.0f, "%.2f");
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##bloom", &s.bloom);
        ImGui::BeginDisabled(!s.bloom);
        drawPropertyLabel("Strength"); ImGui::SliderFloat("##bloomS", &s.bloomStrength, 0.0f, 0.5f, "%.3f");
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Reflection Probes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##probes", &s.probes);
        ImGui::TextDisabled("Local IBL + parallax reflections, blended over the global IBL.");
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset to Defaults")) s = RenderSettings{};

    ImGui::End();
}

} // namespace Engine
