#include "panels/render_settings_panel.h"

#include <algorithm>
#include <iterator>

#include <imgui.h>

#include "command/component_edit.h"
#include "command/editor_commands.h"

#include "core/system.h"
#include "editor_state.h"
#include "ui/editor_style.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_widgets.h"
#include "editor_context.h"
#include "editor_settings.h"

#include "ecs/scene.h"
#include "ecs/component/render/reflection_probe.h"
#include "system/render/render_system.h"

namespace Vkm::Engine {

namespace {

// A square button in @p color, filled while @p shown and outlined while not.
bool gridToggle(const char* name, const ImVec4& color, const ImVec4& hover, bool* shown, const char* what) {
    const ImVec4 off      = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 offHover = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
    ImGui::PushStyleColor(ImGuiCol_Button, *shown ? color : off);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, *shown ? hover : offHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, hover);
    ImGui::PushStyleColor(ImGuiCol_Text, *shown ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : hover);
    const float height  = ImGui::GetFrameHeight();
    const float padding = ImGui::GetStyle().FramePadding.x * 2.0f;
    const float width   = std::max(height, ImGui::CalcTextSize(name).x + padding);
    const bool  clicked = ImGui::Button(name, ImVec2(width, height));
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered()) {
        const char* verb = *shown ? "Hide" : "Show";
        ImGui::SetTooltip("%s the %s. Two axes on show their plane: X and Z the ground", verb, what);
    }
    if (clicked) *shown = !*shown;
    return clicked;
}

// The grid's switches, one per axis in its own colour; two on show their plane.
void gridRow(RenderSettings& s) {
    struct Toggle {
        const char* name;
        bool*       shown;
        const char* what;
    };
    const Toggle axes[3] = {
        {"X", &s.gridAxisX, "X axis"},
        {"Y", &s.gridAxisY, "Y axis"},
        {"Z", &s.gridAxisZ, "Z axis"},
    };
    propRow("World Grid", nullptr, [&] {
        bool changed = false;
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine(0, EditorStyle::px(4.0f));
            const ImVec4 color = EditorStyle::axisVec4(i);
            const ImVec4 hover = EditorStyle::axisVec4(i, 25);
            changed |= gridToggle(axes[i].name, color, hover, axes[i].shown, axes[i].what);
        }
        return changed;
    });
}

} // namespace

void RenderSettingsPanel::draw(EditorContext& ec) {
    EditorState& state = ec.state;

    const ImVec2 size(EditorStyle::px(360.0f), EditorStyle::px(480.0f));
    if (!beginToolWindow("Render Settings", state.showRenderSettings, size)) return;

    RenderSettings& s = ec.frame.render;

    if (beginComponentCard("Output", EditorStyle::Accent::QUALITY, true)) {
        propEnumCombo("Debug View", s.renderMode);

        // Unlike the debug view, this is stored in project.json and the game renders with it.
        const char* tonemapTooltip =
            "How linear HDR is landed into the display range. Reinhard "
            "desaturates bright colour; ACES rolls it off warm; Khronos "
            "PBR Neutral holds an object's authored albedo as it brightens";
        propEnumCombo("Tonemap", s.tonemap, tonemapTooltip);
        const char* exposureTooltip =
            "A fixed exposure in stops, applied before the tonemap: +1 doubles the light "
            "the curve sees, -1 halves it. Authored, never adapted";
        propSlider("Exposure", &s.exposure, -8.0f, 8.0f, "%+.1f EV", exposureTooltip);

        gridRow(s);

        static const char* const MSAA_LABELS[] = { "Off", "2x MSAA", "4x MSAA", "8x MSAA" };
        propValueCombo(
            "Anti-Aliasing",
            MSAA_LABELS,
            RenderSettings::MSAA_SAMPLE_COUNTS,
            &s.msaaSamples,
            "Scene-pass MSAA; the post chain runs on the resolved buffer"
        );

        // One list, two fields: past Bilinear each row is Trilinear plus an anisotropy degree.
        struct FilterEntry {
            const char*      label;
            TextureFiltering mode;
            uint32_t         degree;
        };
        static const FilterEntry FILTERS[] = {
            {"Nearest",           TextureFiltering::Nearest,   1u},
            {"Bilinear",          TextureFiltering::Bilinear,  1u},
            {"Trilinear",         TextureFiltering::Trilinear, 1u},
            {"Anisotropic 2x",    TextureFiltering::Trilinear, 2u},
            {"Anisotropic 4x",    TextureFiltering::Trilinear, 4u},
            {"Anisotropic 8x",    TextureFiltering::Trilinear, 8u},
            {"Anisotropic 16x",   TextureFiltering::Trilinear, 16u},
        };
        constexpr int FILTER_COUNT = static_cast<int>(std::size(FILTERS));
        constexpr int FIRST_ANISO  = 3;

        // Offer only what the driver reports; the backend clamps a stored level past it.
        const uint32_t ceiling = ec.renderSystem.maxAnisotropy();
        int count = FIRST_ANISO;
        while (count < FILTER_COUNT && FILTERS[count].degree <= ceiling) ++count;

        // The highest offered degree the stored one reaches, not an exact match: a file asking
        // for 16x here shows what the driver actually gives.
        int current = FIRST_ANISO - 1;
        for (int i = 0; i < count; ++i) {
            if (FILTERS[i].mode != s.textureFiltering) continue;
            if (s.textureFiltering != TextureFiltering::Trilinear) {
                current = i;
                break;
            }
            if (FILTERS[i].degree <= s.textureAnisotropy) current = i;
        }

        const char* labels[FILTER_COUNT];
        for (int i = 0; i < count; ++i) labels[i] = FILTERS[i].label;

        const char* filteringTooltip =
            "How a texel is fetched, for every texture that has not "
            "pinned its own filter. Anisotropy samples along a stretched "
            "footprint, so ground and road surfaces keep their detail "
            "into the distance";
        if (propIndexCombo("Filtering", labels, count, &current, filteringTooltip)) {
            s.textureFiltering  = FILTERS[current].mode;
            s.textureAnisotropy = FILTERS[current].degree;
        }
    }
    endComponentCard();

    if (beginComponentCard("Ambient Occlusion", EditorStyle::Accent::EFFECT, true)) {
        ImGui::PushID("gtao");
        propCheckbox("Enabled", &s.gtao, "Ground-truth ambient occlusion, applied to the indirect term");
        ImGui::BeginDisabled(!s.gtao);
        propDrag(
            "Radius",
            &s.gtaoRadius,
            0.01f,
            RenderSettings::MIN_GTAO_RADIUS,
            RenderSettings::MAX_GTAO_RADIUS,
            "%.2f",
            "World-space sample radius"
        );
        propSlider("Intensity", &s.gtaoIntensity, 0.0f, 3.0f, "%.2f", "Occlusion strength");
        propSlider("Power", &s.gtaoPower, 0.5f, 4.0f, "%.2f", "Contrast curve on the occlusion factor");
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    endComponentCard();

    if (beginComponentCard("Bloom", EditorStyle::Accent::EFFECT, true)) {
        ImGui::PushID("bloom");
        propCheckbox("Enabled", &s.bloom, "Mip-chain bloom, blended in composite");
        ImGui::BeginDisabled(!s.bloom);
        propSlider(
            "Strength",
            &s.bloomStrength,
            0.0f,
            0.5f,
            "%.3f",
            "Blend amount (linear HDR, pre-tonemap)"
        );
        const char* thresholdTooltip =
            "Bright-pass threshold on a pixel's brightest channel (linear HDR), so a saturated "
            "colour blooms as readily as white";
        propSlider("Threshold", &s.bloomThreshold, 0.0f, 4.0f, "%.2f", thresholdTooltip);
        propSlider("Knee", &s.bloomKnee, 0.0f, 1.0f, "%.2f", "Soft-knee width around the threshold");
        propSlider(
            "Radius",
            &s.bloomRadius,
            0.001f,
            0.02f,
            "%.4f",
            "Upsample tent-filter radius, as a fraction of the frame's width"
        );
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    endComponentCard();

    if (beginComponentCard("Shadows", EditorStyle::Accent::EFFECT, true)) {
        static const char* const SHADOW_RES_LABELS[] = { "Low (1024)", "Medium (2048)", "High (4096)" };
        static const uint32_t    SHADOW_RES_VALUES[] = { 1024u, 2048u, 4096u };
        propValueCombo(
            "Atlas Resolution",
            SHADOW_RES_LABELS,
            SHADOW_RES_VALUES,
            &s.shadowResolution,
            "The largest shadow tile, the sun's near cascades' - usually the frame's main GPU cost lever"
        );
    }
    endComponentCard();

    if (beginComponentCard("Screen-Space Reflections", EditorStyle::Accent::EFFECT, true)) {
        ImGui::PushID("ssr");
        propCheckbox(
            "Enabled",
            &s.ssr,
            "Glossy surfaces reflect what the screen showed, over the probes and the sky"
        );
        ImGui::BeginDisabled(!s.ssr);
        propSlider(
            "Max Roughness",
            &s.ssrMaxRoughness,
            0.05f,
            1.0f,
            "%.2f",
            "Rougher surfaces reflect the probes and the sky alone; each traced surface costs a ray"
        );
        propDrag(
            "Max Distance",
            &s.ssrMaxDistance,
            0.5f,
            1.0f,
            500.0f,
            "%.1f",
            "World-space length a reflection ray is traced before it gives up"
        );
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    endComponentCard();

    if (beginComponentCard("Reflection Probes", EditorStyle::Accent::EFFECT, true)) {
        propCheckbox("Enabled", &s.probes, "Local IBL + parallax reflections, blended over the global IBL");
        if (ImGui::Button("Bake All Probes", ImVec2(-1, 0))) {
            // bakeVersion is reflected: bump it through editStep, which records a prefab instance's override.
            auto batch = std::make_unique<CompositeCommand>("Bake all probes");
            ec.frame.scene.forEach<ReflectionProbe>([&](EntityId probeId, ReflectionProbe& probe) {
                const ReflectionProbe before = probe;
                ++probe.bakeVersion;
                auto step = editStep<ReflectionProbe>(
                    ec.frame.scene,
                    ec.frame.resources,
                    probeId,
                    before,
                    probe,
                    "Bake probe"
                );
                batch->add(std::move(step));
            });
            if (!batch->empty()) ec.state.pushStep(std::move(batch));
        }
    }
    endComponentCard();

    if (beginComponentCard("Culling", EditorStyle::Accent::QUALITY, true)) {
        // Stored on RenderSettings; applied before anything reaches the render pipeline.
        ImGui::PushID("cull");
        propDrag(
            "Max Distance",
            &s.cullMaxDistance,
            5.0f,
            1.0f,
            10000.0f,
            "%.0f",
            "Entities beyond this camera distance are culled"
        );
        propSlider(
            "Min Screen Size",
            &s.cullMinPixels,
            0.0f,
            32.0f,
            "%.1f px",
            "Entities smaller than this on screen are culled; 0 disables"
        );
        ImGui::PopID();
        if (ImGui::Button("Reset Culling", ImVec2(-1, 0))) {
            const RenderSettings defaults;
            s.cullMaxDistance  = defaults.cullMaxDistance;
            s.cullMinPixels    = defaults.cullMinPixels;
        }
    }
    endComponentCard();

    ImGui::Spacing();
    if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0))) m_confirmReset = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset every render setting; culling has its own reset above");
    if (beginDialog("Reset Render Settings", m_confirmReset)) {
        ImGui::TextUnformatted("Reset all render settings to their defaults?");
        const char* resetNote =
            "The cull distance and screen-size threshold go back too;\n"
            "they are part of these settings.";
        ImGui::TextDisabled("%s", resetNote);
        if (dialogButtons(m_confirmReset, "Reset") == DialogResult::Confirm) {
            s = RenderSettings{};
            EditorSettings::applyViewDefaults(s);
        }
        endDialog();
    }

    ImGui::End();
}

} // namespace Vkm::Engine
