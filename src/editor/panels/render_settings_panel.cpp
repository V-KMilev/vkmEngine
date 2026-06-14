#include "panels/render_settings_panel.h"

#include "framework/editor_common.h"
#include "framework/editor_context.h"

#include "ecs/scene.h"
#include "ecs/component/reflection_probe.h"
#include "system/render/render_system.h"
#include "system/visibility/visibility_system.h"

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

    // Order must match RenderMode in render_settings.h.
    static const char* const kRenderModes[] = {
        "Default", "Depth", "Normals", "Roughness", "Metalness",
        "Ambient Occlusion", "Bloom", "Shadow Atlas",
    };
    int modeIdx = static_cast<int>(s.renderMode);
    drawPropertyLabel("Debug View");
    if (ImGui::Combo("##renderMode", &modeIdx, kRenderModes, IM_ARRAYSIZE(kRenderModes))) {
        s.renderMode = static_cast<RenderMode>(modeIdx);
    }
    ImGui::Checkbox("World Grid", &s.grid);
    ImGui::SameLine();
    ImGui::Checkbox("FXAA", &s.fxaa);
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Ambient Occlusion (GTAO)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##gtao", &s.gtao);
        ImGui::BeginDisabled(!s.gtao);
        drawPropertyLabel("Radius");    ImGui::DragFloat("##gtaoR", &s.gtaoRadius, 0.01f, 0.05f, 5.0f, "%.2f");
        drawPropertyLabel("Intensity"); ImGui::SliderFloat("##gtaoI", &s.gtaoIntensity, 0.0f, 3.0f, "%.2f");
        drawPropertyLabel("Power");     ImGui::SliderFloat("##gtaoP", &s.gtaoPower, 0.5f, 4.0f, "%.2f");
        drawPropertyLabel("Bias");      ImGui::SliderFloat("##gtaoB", &s.gtaoBias, 0.0f, 0.2f, "%.3f");
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
        drawPropertyLabel("Intensity");    ImGui::SliderFloat("##mbI", &s.motionBlurIntensity, 0.0f, 3.0f, "%.2f");
        drawPropertyLabel("Max Velocity"); ImGui::SliderFloat("##mbV", &s.motionBlurMaxVelocity, 0.0f, 0.2f, "%.3f");
        drawPropertyLabel("Samples");      ImGui::SliderInt("##mbN", &s.motionBlurSamples, 1, 32);
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##bloom", &s.bloom);
        ImGui::BeginDisabled(!s.bloom);
        drawPropertyLabel("Strength");  ImGui::SliderFloat("##bloomS", &s.bloomStrength, 0.0f, 0.5f, "%.3f");
        drawPropertyLabel("Threshold"); ImGui::SliderFloat("##bloomT", &s.bloomThreshold, 0.0f, 4.0f, "%.2f");
        drawPropertyLabel("Knee");      ImGui::SliderFloat("##bloomK", &s.bloomKnee, 0.0f, 1.0f, "%.2f");
        drawPropertyLabel("Radius");    ImGui::SliderFloat("##bloomR", &s.bloomRadius, 0.001f, 0.02f, "%.4f");
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Per-tile atlas resolution. Higher is crisper but the shadow pass is
        // usually the frame's dominant GPU cost, so this is the main FPS lever.
        static const char* const kShadowResLabels[] = { "Low (1024)", "Medium (2048)", "High (4096)" };
        static const uint32_t    kShadowResValues[] = { 1024u, 2048u, 4096u };
        int resIdx = 2;
        for (int i = 0; i < 3; ++i) {
            if (kShadowResValues[i] == s.shadowResolution) resIdx = i;
        }
        drawPropertyLabel("Atlas Resolution");
        if (ImGui::Combo("##shadowRes", &resIdx, kShadowResLabels, IM_ARRAYSIZE(kShadowResLabels))) {
            s.shadowResolution = kShadowResValues[resIdx];
        }
    }

    if (ImGui::CollapsingHeader("Reflection Probes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##probes", &s.probes);
        ImGui::TextDisabled("Local IBL + parallax reflections, blended over the global IBL.");
        if (ImGui::Button("Bake All Probes", ImVec2(-1, 0))) {
            ec.frame.scene.forEach<ReflectionProbe>(
                [](EntityId, ReflectionProbe& probe) { probe.bakeVersion++; });
            ec.state.markSceneDirty();
        }
    }

    if (ImGui::CollapsingHeader("Culling", ImGuiTreeNodeFlags_DefaultOpen)) {
        // VisibilitySystem thresholds applied before anything reaches the
        // render pipeline: entities past the distance, or smaller than the
        // screen-size floor, are skipped. The cheapest FPS lever in a dense scene.
        VisibilitySystem::Settings& vis = ec.visibilitySystem.getSettings();
        drawPropertyLabel("Max Distance");
        ImGui::DragFloat("##cullDist", &vis.maxDistance, 5.0f, 1.0f, 10000.0f, "%.0f");
        drawPropertyLabel("Min Screen Size");
        ImGui::SliderFloat("##cullPixels", &vis.minPixels, 0.0f, 32.0f, "%.1f px");
        ImGui::TextDisabled("Min Screen Size 0 disables screen-size culling.");
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset to Defaults")) {
        s = RenderSettings{};
        ec.visibilitySystem.setSettings({});
    }

    ImGui::End();
}

} // namespace Engine
