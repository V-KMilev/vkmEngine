#include "panels/render_settings_panel.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>

#include <imgui.h>

#include "undo/component_edit.h"
#include "undo/composite_command.h"

#include "core/graphics.h"
#include "core/system.h"
#include "editor_state.h"
#include "session/project/content_pack.h"
#include "ui/style/editor_style.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_widgets.h"
#include "ui/rows.h"
#include "editor_context.h"
#include "editor_settings.h"

#include "ecs/scene.h"
#include "ecs/component/light/reflection_probe.h"
#include "import/import_formats.h"
#include "io/project/project.h"
#include "io/project/roots.h"
#include "system/render/render_system.h"

namespace Vkm::Engine {

namespace {

// Where the engine's grading pack, content/grading, puts its neutral strip in a project.
constexpr const char* NEUTRAL_GRADING = "assets/grading/neutral.png";

// Bring the engine's grading pack into the project, telling @p state what came in or why nothing
// could: true once the project holds the neutral strip. A strip the project has is kept, edits
// and all.
bool bringNeutralGrading(EditorState& state) {
    const std::filesystem::path pack = ContentPack::shipped("grading");
    std::string error;
    const int copied = ContentPack::bringInto(pack, Roots::projectRoot(), error);
    if (copied < 0) {
        state.toasts.push(ToastKind::Error, "Could not bring the neutral grading table in: " + error);
        return false;
    }
    if (copied > 0) state.toasts.push(ToastKind::Info, std::string("Brought in ") + NEUTRAL_GRADING);
    return true;
}

} // namespace

void RenderSettingsPanel::keepInProject(EditorContext& ec, bool playing) {
    const std::filesystem::path root = Roots::projectRoot();
    if (root.empty()) return;
    // Not mid-drag, which would write the file every frame of it.
    const RenderSettings& project = ec.frame.graphics.project();
    RenderSettings& saved = ec.state.projectSaved.render;
    if (playing || ImGui::IsAnyItemActive() || sameProjectRender(project, saved)) return;
    if (!saveProjectRender(root, project)) {
        const char* failed = "Could not write the render settings to project.json";
        ec.state.toasts.push(ToastKind::Error, failed);
    }
    // Once either way: a file that will not take them is not asked again until the next edit.
    saved = project;
}

void RenderSettingsPanel::drawPreview(EditorContext& ec, bool playing) {
    static const char* const TIERS[] = {"Project", "Performance", "Low", "Medium", "High", "Max"};
    static_assert(std::size(TIERS) == static_cast<size_t>(GraphicsPreset::Count) + 1, "a row per tier");
    std::optional<GraphicsPreset>& preview = ec.state.previewTier;
    int shown = preview ? static_cast<int>(*preview) + 1 : 0;
    ImGui::BeginDisabled(playing);
    const bool picked = propIndexCombo(
        "Preview Tier",
        TIERS,
        static_cast<int>(std::size(TIERS)),
        &shown,
        "See the scene as a player's quality tier draws it (Graphics::setQuality over these "
            "settings); nothing is saved while one shows"
    );
    ImGui::EndDisabled();
    if (picked) {
        if (shown == 0) {
            ec.state.endRenderPreview(ec.frame.graphics);
        } else {
            preview = static_cast<GraphicsPreset>(shown - 1);
            ec.frame.graphics.setQuality(*preview);
        }
    }
    if (preview) {
        ImGui::TextColored(
            EditorStyle::WARNING,
            "Showing the %s tier. The settings below are the project's, put back on Project.",
            TIERS[shown]
        );
    }
}

void RenderSettingsPanel::drawGrading(EditorContext& ec, Rows<RenderSettings>& rows) {
    // Picked, brought in or cleared, never typed: the frame reads each path it is given from disk.
    rows.rowWith<&RenderSettings::gradingLut>([&](std::string& table, const FieldInfo&) {
        const bool  none    = table.empty();
        const float gap     = ImGui::GetStyle().ItemSpacing.x;
        const float button  = ImGui::GetFrameHeight() * 2.4f;
        const float buttons = 2.0f * button + gap;
        const float left    = ImGui::GetCursorPosX();
        const float width   = ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();
        clippedLine(none ? "None" : table.c_str(), width - buttons - gap, none);
        ImGui::SameLine(left + width - buttons);
        if (ImGui::Button("Pick...", ImVec2(button, 0.0f))) {
            FilePicker::Options options;
            options.title      = "Pick Grading Table";
            options.root       = Roots::projectRoot();
            options.recursive  = true;
            options.extensions = {IMAGE_EXTENSIONS.begin(), IMAGE_EXTENSIONS.end()};
            options.hint       = "A strip of N squares N texels on a side: 1024 by 32 for 32";
            m_gradingPicker.open(options);
        }
        ImGui::SameLine();
        if (!none) {
            const bool cleared = ImGui::Button("Clear", ImVec2(button, 0.0f));
            if (cleared) table.clear();
            return cleared;
        }
        const bool neutral = ImGui::Button("Neutral", ImVec2(button, 0.0f)) && bringNeutralGrading(ec.state);
        if (neutral) table = NEUTRAL_GRADING;
        return neutral;
    });
    std::string picked;
    if (m_gradingPicker.draw(picked)) {
        rows.value().gradingLut = picked;
        rows.edited<&RenderSettings::gradingLut>();
    }
    ImGui::BeginDisabled(rows.value().gradingLut.empty());
    rows.slider<&RenderSettings::gradingStrength>("%.2f");
    ImGui::EndDisabled();
}

void RenderSettingsPanel::draw(EditorContext& ec, bool playing) {
    EditorState& state = ec.state;

    // The window's own: closing it shows the project's settings again, as Play does.
    if (!state.showRenderSettings) state.endRenderPreview(ec.frame.graphics);

    const ImVec2 size(EditorStyle::px(360.0f), EditorStyle::px(480.0f));
    if (!beginToolWindow("Render Settings", state.showRenderSettings, size)) return;

    drawPreview(ec, playing);
    const bool previewing = state.previewTier.has_value();
    ImGui::BeginDisabled(previewing);

    // Outside a session the project's settings, which the frame draws; in one, the game's.
    Graphics& graphics = ec.frame.graphics;
    const RenderSettings shownAs = playing ? graphics.settings() : graphics.project();
    RenderSettings s = shownAs;

    Rows<RenderSettings> rows(s, {}, nullptr);
    if (beginComponentCard("Output", EditorIcon::None, true)) {
        rows.row<&RenderSettings::showFrameRate>();
        rows.row<&RenderSettings::tonemap>();
        rows.slider<&RenderSettings::exposure>("%+.1f EV");
        rows.row<&RenderSettings::autoExposure>();
        ImGui::BeginDisabled(!s.autoExposure);
        rows.slider<&RenderSettings::exposureMinimum>("%+.1f EV");
        rows.slider<&RenderSettings::exposureMaximum>("%+.1f EV");
        ImGui::EndDisabled();
        drawGrading(ec, rows);
        rows.rowWith<&RenderSettings::msaaSamples>([](uint32_t& samples, const FieldInfo&) {
            static const char* const LABELS[] = {"Off", "2x", "4x", "8x"};
            return valueCombo("##v", LABELS, RenderSettings::MSAA_SAMPLE_COUNTS, &samples);
        });
        rows.slider<&RenderSettings::renderScale>("%.2f");
        // Only what the driver reports; the backend clamps a stored degree past it.
        const uint32_t ceiling = ec.renderSystem.maxAnisotropy();
        rows.rowWith<&RenderSettings::textureAnisotropy>([&](uint32_t& degree, const FieldInfo&) {
            static const char* const LABELS[] = {"Off", "2x", "4x", "8x", "16x"};
            static const uint32_t DEGREES[] = {1, 2, 4, 8, 16};
            int offered = 1;
            while (offered < 5 && DEGREES[offered] <= ceiling) ++offered;
            // The highest offered degree the stored one reaches: a file asking for 16x shows what
            // the driver gives.
            int current = 0;
            for (int i = 0; i < offered; ++i) {
                if (DEGREES[i] <= degree) current = i;
            }
            if (!comboList("##v", &current, LABELS, offered)) return false;
            degree = DEGREES[current];
            return true;
        });
    }
    endComponentCard();

    if (beginComponentCard("Effects", EditorIcon::None, true)) {
        rows.row<&RenderSettings::gtao>();
        rows.row<&RenderSettings::bloom>();
        rows.row<&RenderSettings::ssr>();
        ImGui::BeginDisabled(!s.ssr);
        rows.row<&RenderSettings::ssrHalfResolution>();
        ImGui::EndDisabled();
        rows.row<&RenderSettings::occlusionCulling>();
    }
    endComponentCard();

    if (beginComponentCard("Shadows", EditorIcon::None, true)) {
        rows.rowWith<&RenderSettings::shadowResolution>([](uint32_t& texels, const FieldInfo&) {
            static const char* const LABELS[] = {"1024", "2048", "4096"};
            return valueCombo("##v", LABELS, RenderSettings::SHADOW_RESOLUTIONS, &texels);
        });
        rows.row<&RenderSettings::softShadows>();
    }
    endComponentCard();

    if (beginComponentCard("Reflection Probes", EditorIcon::None, true)) {
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

    ImGui::Spacing();
    if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0))) m_confirmReset = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset every render setting");
    if (beginDialog("Reset Render Settings", m_confirmReset)) {
        ImGui::TextUnformatted("Reset all render settings to their defaults?");
        if (dialogButtons(m_confirmReset, "Reset") == DialogResult::Confirm) {
            s = RenderSettings{};
        }
        endDialog();
    }
    if (!previewing && !sameProjectRender(s, shownAs)) {
        if (playing) graphics.set(s);
        else graphics.setProject(s);
    }

    ImGui::EndDisabled();
    // Unlike Project Settings, which waits for Save: say so where the edits are made.
    if (playing) {
        ImGui::TextDisabled("In a session these are the game's; Stop puts the project's back.");
    } else if (!previewing) {
        ImGui::TextDisabled("Saved into project.json as each edit ends.");
    }
    if (!previewing) keepInProject(ec, playing);
    ImGui::End();
}

} // namespace Vkm::Engine
