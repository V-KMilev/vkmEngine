#include "panels/render_settings_panel.h"

#include "framework/editor_common.h"
#include "ui/editor_style.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_widgets.h"
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

    RenderSettings& s = ec.renderSystem.getSettings();

    if (beginComponentCard("Output", EditorStyle::Accent::Quality, true)) {
        propEnumCombo("Debug View", s.renderMode);
        propCheckbox("World Grid", &s.grid, "World-space ground grid overlay (editor aid)");

        // MSAA sample count for the scene pass (machine-quality). The whole post
        // chain runs on the resolved single-sample buffer, so only geometry-edge
        // cost scales with the sample count.
        static const char* const kMsaaLabels[] = { "Off", "2x MSAA", "4x MSAA", "8x MSAA" };
        static const uint32_t    kMsaaValues[] = { 1u, 2u, 4u, 8u };
        propValueCombo("Anti-Aliasing", kMsaaLabels, kMsaaValues, 4, &s.msaaSamples,
                       "Scene-pass MSAA; the post chain runs on the resolved buffer");
    }
    endComponentCard();

    if (beginComponentCard("Ambient Occlusion", EditorStyle::Accent::Effect, true)) {
        ImGui::PushID("gtao");
        propCheckbox("Enabled", &s.gtao, "Ground-truth ambient occlusion, applied to the indirect term");
        ImGui::BeginDisabled(!s.gtao);
        propDrag("Radius", &s.gtaoRadius, 0.01f, 0.05f, 5.0f, "%.2f", "World-space sample radius");
        propSlider("Intensity", &s.gtaoIntensity, 0.0f, 3.0f, "%.2f", "Occlusion strength");
        propSlider("Power", &s.gtaoPower, 0.5f, 4.0f, "%.2f", "Contrast curve on the occlusion factor");
        propSlider("Bias", &s.gtaoBias, 0.0f, 0.2f, "%.3f", "View-space self-occlusion guard");
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    endComponentCard();

    if (beginComponentCard("Bloom", EditorStyle::Accent::Effect, true)) {
        ImGui::PushID("bloom");
        propCheckbox("Enabled", &s.bloom, "Mip-chain bloom, blended in composite");
        ImGui::BeginDisabled(!s.bloom);
        propSlider("Strength", &s.bloomStrength, 0.0f, 0.5f, "%.3f", "Blend amount (linear HDR, pre-tonemap)");
        propSlider("Threshold", &s.bloomThreshold, 0.0f, 4.0f, "%.2f", "Bright-pass threshold (HDR luminance)");
        propSlider("Knee", &s.bloomKnee, 0.0f, 1.0f, "%.2f", "Soft-knee width around the threshold");
        propSlider("Radius", &s.bloomRadius, 0.001f, 0.02f, "%.4f", "Upsample tent-filter radius (UV space)");
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    endComponentCard();

    if (beginComponentCard("Shadows", EditorStyle::Accent::Effect, true)) {
        // Per-tile atlas resolution. Higher is crisper but the shadow pass is
        // usually the frame's dominant GPU cost, so this is the main FPS lever.
        static const char* const kShadowResLabels[] = { "Low (1024)", "Medium (2048)", "High (4096)" };
        static const uint32_t    kShadowResValues[] = { 1024u, 2048u, 4096u };
        propValueCombo("Atlas Resolution", kShadowResLabels, kShadowResValues, 3, &s.shadowResolution,
                       "Per-tile shadow map size - usually the frame's main GPU cost lever");

        // Screen-space contact shadows for the sun (small-scale occlusion the
        // cascades are too coarse for).
        propCheckbox("Contact Shadows", &s.contactShadows,
                     "Screen-space sun occlusion the cascades are too coarse for");
        ImGui::BeginDisabled(!s.contactShadows);
        propSlider("Contact Length", &s.contactShadowLength, 0.05f, 2.0f, "%.2f", "View-space ray length toward the sun");
        propSlider("Contact Thickness", &s.contactShadowThickness, 0.05f, 2.0f, "%.2f", "Max occluder thickness counted as shadow");
        ImGui::EndDisabled();
    }
    endComponentCard();

    if (beginComponentCard("Reflection Probes", EditorStyle::Accent::Effect, true)) {
        propCheckbox("Enabled", &s.probes, "Local IBL + parallax reflections, blended over the global IBL");
        if (ImGui::Button("Bake All Probes", ImVec2(-1, 0))) {
            ec.frame.scene.forEach<ReflectionProbe>(
                [](EntityId, ReflectionProbe& probe) { probe.bakeVersion++; });
            ec.state.markSceneDirty();
        }
    }
    endComponentCard();

    if (beginComponentCard("Culling", EditorStyle::Accent::Quality, true)) {
        // VisibilitySystem thresholds applied before anything reaches the
        // render pipeline: entities past the distance, or smaller than the
        // screen-size floor, are skipped. The cheapest FPS lever in a dense scene.
        VisibilitySystem::Settings& vis = ec.visibilitySystem.getSettings();
        ImGui::PushID("cull");
        propDrag("Max Distance", &vis.maxDistance, 5.0f, 1.0f, 10000.0f, "%.0f",
                 "Entities beyond this camera distance are culled");
        propSlider("Min Screen Size", &vis.minPixels, 0.0f, 32.0f, "%.1f px",
                   "Entities smaller than this on screen are culled; 0 disables");
        ImGui::PopID();
        if (ImGui::Button("Reset Culling", ImVec2(-1, 0))) {
            ec.visibilitySystem.setSettings({});
        }
    }
    endComponentCard();

    ImGui::Spacing();
    if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0))) m_confirmReset = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset every render setting; culling has its own reset above");
    if (beginDialog("Reset Render Settings", m_confirmReset)) {
        ImGui::TextUnformatted("Reset all render settings to their defaults?");
        ImGui::TextDisabled("Culling keeps its current values.");
        if (dialogButtons(m_confirmReset, "Reset") == DialogResult::Confirm) {
            s = RenderSettings{};
            s.grid = true;  // the editor's default, not the engine's
        }
        endDialog();
    }

    ImGui::End();
}

} // namespace Engine
