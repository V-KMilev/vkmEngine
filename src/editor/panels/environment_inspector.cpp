#include "panels/environment_inspector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <system_error>

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include "framework/editor_common.h"
#include "ui/editor_style.h"
#include "system/render/render_view.h"          // EnvironmentConfig
#include "system/visibility/visibility.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "system/render/render_backend.h"       // getAdaptedLuminance() for the EV readout
#include "system/render/render_graph.h"
#include "system/render/render_pass.h"
#include "loader/environment_loaders.h"         // loadColorLUT for thumbnail
#include "texture/gl_texture.h"                 // Core::Texture2D for thumbnail

namespace Engine {

EnvironmentInspector::EnvironmentInspector()  = default;
EnvironmentInspector::~EnvironmentInspector() = default;

namespace {

    // Per-group accent rail colors - same palette the Inspector uses for
    // Transform/Light/Camera so the Environment reads as part of the editor,
    // not a bolted-on panel. Seven cards now (was five): split World out of
    // Lighting, split Screen-Space FX out of Post, give each card a distinct
    // hue so the user can scan-locate by colour.
    const ImVec4 ACCENT_WORLD    = ImVec4(0.95f, 0.62f, 0.30f, 1.0f);  // warm sun
    const ImVec4 ACCENT_LIGHTING = ImVec4(1.00f, 0.80f, 0.22f, 1.0f);  // gold
    const ImVec4 ACCENT_CAMERA   = ImVec4(0.30f, 0.78f, 0.80f, 1.0f);  // cyan
    const ImVec4 ACCENT_POST     = EditorStyle::ACCENT;                // blue (image-finishing)
    const ImVec4 ACCENT_SSFX     = ImVec4(0.45f, 0.80f, 0.55f, 1.0f);  // green (screen-space FX)
    const ImVec4 ACCENT_DIAG     = ImVec4(0.55f, 0.58f, 0.62f, 1.0f);  // gray
    const ImVec4 ACCENT_PERF     = ImVec4(0.64f, 0.44f, 0.86f, 1.0f);  // purple

    // An effect sub-block inside a group card: a hairline divider, an
    // optional enable checkbox, then the title in header text. Body is
    // always laid out (the caller wraps it in BeginDisabled when off) so
    // nothing jumps - the Unreal/Unity "greyed override" pattern. Returns
    // true so existing `if (cardHeader(...)) { ... }` call sites still read.
    bool cardHeader(const char* id, const char* title, bool* enabled) {
        ImGui::PushID(id);
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float  w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(p.x, p.y), ImVec2(p.x + w, p.y),
            ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        if (enabled) {
            ImGui::Checkbox("##en", enabled);
            ImGui::SameLine(0.0f, 8.0f);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Spacing();
        return true;
    }

    // Labelled bounded slider with a hover tooltip. Returns true on change.
    bool sliderF(
        const char* label,
        const char* id,
        float* v,
        float lo,
        float hi,
        const char* fmt,
        const char* tip,
        bool logarithmic = false
    ) {
        drawPropertyLabel(label);
        ImGui::SetNextItemWidth(-1.0f);
        bool ch = ImGui::SliderFloat(id, v, lo, hi, fmt,
            logarithmic ? ImGuiSliderFlags_Logarithmic : 0);
        if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        return ch;
    }

    // "Browse" button + AssetPicker modal. Picker is owned by the panel
    // (one per slot) so its on-open scan cache outlives a single draw.
    void browseButton(
        const char* btnLabel,
        AssetPicker& picker,
        const char* subdir,
        std::initializer_list<const char*> exts,
        std::string& target
    ) {
        if (ImGui::Button(btnLabel)) {
            const std::filesystem::path appRoot = APP_ROOT_DIR;
            picker.options.popupId    = btnLabel;
            picker.options.title      = "Browse";
            picker.options.root       = appRoot / subdir;
            picker.options.recursive  = false;
            picker.options.kind       = AssetPicker::Kind::Files;
            picker.options.extensions.clear();
            for (const char* x : exts) picker.options.extensions.emplace_back(x);
            picker.options.relativeTo = appRoot;
            picker.open();
        }
        std::string picked;
        if (picker.draw(picked)) target = picked;
    }

    enum class Preset { Low, Medium, High, Cinematic };

    // Presets flip effect enables + bloom amount + auto-exposure (predictable;
    // they do not clobber paths, colours, grid or exposure tuning). Auto
    // exposure is on for the higher tiers (Low/Medium stay reference-faithful).
    void applyPreset(EnvironmentConfig& env, Preset p) {
        switch (p) {
            case Preset::Low:
                env.ao.enabled = false; env.ssr.enabled = false; env.taa.enabled = false;
                env.dof.enabled = false; env.motionBlur.enabled = false;
                env.bloom.strength = 0.0f;
                env.exposure.autoExposure = false;
                break;
            case Preset::Medium:
                env.ao.enabled = true;  env.ssr.enabled = false; env.taa.enabled = false;
                env.dof.enabled = false; env.motionBlur.enabled = false;
                env.bloom.strength = 0.03f;
                env.exposure.autoExposure = false;
                break;
            case Preset::High:
                env.ao.enabled = true;  env.ssr.enabled = true;  env.taa.enabled = false;
                env.dof.enabled = false; env.motionBlur.enabled = false;
                env.bloom.strength = 0.04f;
                env.exposure.autoExposure = true;
                break;
            case Preset::Cinematic:
                env.ao.enabled = true;  env.ssr.enabled = true;  env.taa.enabled = true;
                env.dof.enabled = true; env.motionBlur.enabled = true;
                env.bloom.strength = 0.06f;
                env.exposure.autoExposure = true;
                break;
        }
    }

    // Which preset (if any) the current env exactly matches. -1 = Custom.
    int detectPreset(const EnvironmentConfig& env) {
        auto matches = [&](bool ao, bool ssr, bool taa, bool dof,
                           bool mb, float bloom, bool ae) {
            float d = env.bloom.strength - bloom;
            if (d < 0.0f) d = -d;
            return env.ao.enabled == ao && env.ssr.enabled == ssr && env.taa.enabled == taa &&
                   env.dof.enabled == dof && env.motionBlur.enabled == mb &&
                   env.exposure.autoExposure == ae && d < 5e-4f;
        };
        if (matches(false, false, false, false, false, 0.00f, false)) return 0; // Low
        if (matches(true,  false, false, false, false, 0.03f, false)) return 1; // Medium
        if (matches(true,  true,  false, false, false, 0.04f, true )) return 2; // High
        if (matches(true,  true,  true,  true,  true,  0.06f, true )) return 3; // Cinematic
        return -1;
    }

} // namespace

bool EnvironmentInspector::drawPresetBar(EnvironmentConfig& env) {
    bool changed = false;
    const int active = detectPreset(env);

    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
    ImGui::TextUnformatted("Quality Preset");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled(active < 0 ? "(Custom)" : "");

    static const char* PRESET_NAMES[4] = { "Low", "Medium", "High", "Cinematic" };

    const float h      = ImGui::GetFrameHeight() * 1.25f;
    const float resetW = 64.0f;
    const float avail  = ImGui::GetContentRegionAvail().x;
    const float segW   = (avail - resetW - 6.0f) / 4.0f;

    // Segmented control: flush rectangular cells (FrameRounding 0 so adjacent
    // rounded corners don't leave notch gaps at the seams), accent fill on
    // the active tier, card-header fill otherwise.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    for (int i = 0; i < 4; ++i) {
        const bool on = (active == i);
        ImGui::PushStyleColor(ImGuiCol_Button,
            on ? EditorStyle::ACCENT : EditorStyle::CARD_HEADER);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            on ? EditorStyle::ACCENT_HOV : EditorStyle::CARD_HEADER_HOV);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            on ? EditorStyle::ACCENT : EditorStyle::CARD_HEADER_ACT);
        if (ImGui::Button(PRESET_NAMES[i], ImVec2(segW, h))) {
            applyPreset(env, static_cast<Preset>(i));
            changed = true;
        }
        ImGui::PopStyleColor(3);
        if (i < 3) ImGui::SameLine(0.0f, 0.0f);
    }
    ImGui::SameLine(0.0f, 6.0f);

    // Ghost "Reset" button (transparent until hovered).
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::Button("Reset", ImVec2(resetW, h))) {
        const std::string ep = env.ibl.path;
        const std::string lp = env.colorGrade.lutPath;
        env = EnvironmentConfig{};
        env.ibl.path = ep;   // keep the asset references
        env.colorGrade.lutPath       = lp;
        changed = true;
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Restore documented defaults (keeps the HDR / LUT paths)");
    ImGui::PopStyleVar(2);
    return changed;
}

bool EnvironmentInspector::drawWorld(EditorContext& /*ec*/, EnvironmentConfig& env) {
    bool changed = false;

    bool       iblOn   = !env.ibl.path.empty();
    const bool iblPrev = iblOn;
    const bool iblOpen = cardHeader("ibl", "Environment Map (IBL)", &iblOn);
    if (iblOn != iblPrev) {
        if (iblOn) {
            env.ibl.path = !m_iblPathMemo.empty()
                ? m_iblPathMemo : std::string("assets/envs/environment.hdr");
        } else {
            m_iblPathMemo = env.ibl.path;
            env.ibl.path.clear();
        }
        changed = true;
    }
    if (iblOpen) {
        ImGui::BeginDisabled(!iblOn);
        // Only refill the edit buffer when the source changes - otherwise an
        // in-flight keystroke is overwritten before the user can press Enter.
        if (env.ibl.path != m_hdrPathLastSync) {
            snprintf(m_hdrPathBuf, sizeof(m_hdrPathBuf), "%s", env.ibl.path.c_str());
            m_hdrPathLastSync = env.ibl.path;
        }
        drawPropertyLabel("HDR Path");
        ImGui::SetNextItemWidth(-150.0f);
        if (ImGui::InputText("##IBLPath", m_hdrPathBuf, sizeof(m_hdrPathBuf),
                ImGuiInputTextFlags_EnterReturnsTrue)) {
            env.ibl.path = m_hdrPathBuf;
            m_hdrPathLastSync = env.ibl.path;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply##IBL")) {
            env.ibl.path = m_hdrPathBuf; m_hdrPathLastSync = env.ibl.path; changed = true;
        }
        ImGui::SameLine();
        {
            const std::string prev = env.ibl.path;
            browseButton("Browse##IBL", m_iblPicker, "assets/envs",
                         {".hdr", ".HDR"}, env.ibl.path);
            if (env.ibl.path != prev) changed = true;
        }
        changed |= sliderF("Intensity", "##IBLInt", &env.ibl.intensity, 0.0f, 5.0f, "%.2f",
                "Strength of image-based ambient + specular");
        if (env.ibl.path.empty())
            ImGui::TextDisabled("No map - flat ambient fallback");
        else
            ImGui::TextDisabled("%s",
                std::filesystem::path(env.ibl.path).filename().string().c_str());
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("amb", "Ambient Light", nullptr)) {
        drawPropertyLabel("Color");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::ColorEdit3("##AmbCol", glm::value_ptr(env.ambient.color),
            ImGuiColorEditFlags_Float);
        changed |= sliderF("Intensity", "##AmbInt", &env.ambient.intensity, 0.0f, 2.0f, "%.3f",
                "Flat ambient used when no IBL map is set");
    }

    ImGui::Spacing();
    if (cardHeader("bg", "Background", nullptr)) {
        drawPropertyLabel("Clear Color");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::ColorEdit3("##ClearCol", glm::value_ptr(env.clearColor),
            ImGuiColorEditFlags_Float);
    }

    ImGui::Spacing();
    if (cardHeader("tonemap", "Tone Mapping", nullptr)) {
        drawPropertyLabel("Curve");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::Combo("##Tonemap", &env.tonemap,
            "AgX (filmic)\0PBR Neutral (albedo-faithful)\0ACES\0Reinhard\0");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Display transform applied at composite. PBR Neutral preserves\n"
                "material colour (matches online glTF viewers); AgX is a\n"
                "desaturating film look.");
    }

    return changed;
}

bool EnvironmentInspector::drawLightingShadows(EditorContext& /*ec*/, EnvironmentConfig& env) {
    bool changed = false;

    if (cardHeader("shadow", "Shadow Quality", nullptr)) {
        static const uint32_t kRes2D[]   = { 512, 1024, 2048, 4096, 8192 };
        static const char*    kRes2DNames = "512\0" "1024\0" "2048\0" "4096\0" "8192\0";
        static const uint32_t kResCube[]  = { 128, 256, 512, 1024, 2048 };
        static const char*    kResCubeNames = "128\0" "256\0" "512\0" "1024\0" "2048\0";

        auto pickIndex = [&](uint32_t current, const uint32_t* arr, int count) {
            int idx = 0;
            int bestDelta = std::abs(static_cast<int>(current) - static_cast<int>(arr[0]));
            for (int i = 1; i < count; ++i) {
                int d = std::abs(static_cast<int>(current) - static_cast<int>(arr[i]));
                if (d < bestDelta) { bestDelta = d; idx = i; }
            }
            return idx;
        };

        int idx2D = pickIndex(env.shadow.atlasRes2D, kRes2D, 5);
        drawPropertyLabel("Directional/Spot");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##Shadow2DRes", &idx2D, kRes2DNames)) {
            env.shadow.atlasRes2D = kRes2D[idx2D];
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Per-layer resolution of the 2D shadow array.\n"
                              "Changes reallocate the depth texture (~%llu MB at current size).",
                              static_cast<unsigned long long>(
                                  static_cast<uint64_t>(env.shadow.atlasRes2D)
                                  * env.shadow.atlasRes2D * 3 / (1024 * 1024)));

        int idxCube = pickIndex(env.shadow.atlasResCube, kResCube, 5);
        drawPropertyLabel("Point (cube)");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##ShadowCubeRes", &idxCube, kResCubeNames)) {
            env.shadow.atlasResCube = kResCube[idxCube];
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Per-face resolution of the point-light cube array.");

        changed |= sliderF("Softness", "##ShadowSoft", &env.shadow.softness,
                0.0f, 1.0f, "%.2f",
                "PCF kernel-width multiplier.\n"
                "Directional/spot: widens the 1.5-texel base disk.\n"
                "Point (cube): distance-scaled angular jitter.");
    }

    ImGui::Spacing();
    if (cardHeader("oit", "Transparency", nullptr)) {
        changed |= ImGui::Checkbox("Weighted-Blended OIT", &env.transparency.useOIT);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Order-independent transparency (McGuire-Bavoil 2013).\n"
                "Off (default): back-to-front sorted alpha-blend.\n"
                "  - supports glass refraction via the scene snapshot.\n"
                "  - sorts wrong for intersecting/overlapping transparents.\n"
                "On: weighted accumulation into MRT, resolved over the scene.\n"
                "  - correct for intersection / particle volumes.\n"
                "  - refraction is bypassed (no behind-snapshot to sample).");
    }

    ImGui::Spacing();
    if (cardHeader("occlusion", "Occlusion", nullptr)) {
        changed |= ImGui::Checkbox("Build Hi-Z pyramid", &env.occlusion.useHiZ);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Build a max-Z depth pyramid from the prepass each frame.\n"
                "The visibility system reads back one mip into\n"
                "OcclusionOracle to AABB-test candidate occludees against\n"
                "the previous frame's depth.");
    }
    return changed;
}

bool EnvironmentInspector::drawCameraFX(EditorContext& ec, EnvironmentConfig& env) {
    FrameContext& ctx = ec.frame;
    bool changed = false;

    // cardHeader flips env.dof.enabled etc. on the user's checkbox click.
    // We can't see the flip return-side, so snapshot the value before each
    // card and OR a "did it change?" into `changed`.
    auto cardWithEnable = [&](const char* id, const char* title, bool* enabled) {
        const bool prev = *enabled;
        const bool open = cardHeader(id, title, enabled);
        if (*enabled != prev) changed = true;
        return open;
    };

    if (cardHeader("exp", "Exposure", nullptr)) {
        const float camExp = (ctx.visibility && ctx.visibility->hasCamera)
            ? ctx.visibility->cameraExposure : 1.0f;
        ImGui::TextDisabled("Manual camera exposure: %.2f (edit on the Camera entity)",
            camExp);

        // Live readout from the auto-exposure pass. Lum is the linear
        // average scene luminance the eye adaptation has converged to; EV
        // is the photographic stop the scene is sitting at (log2(L / 0.18),
        // 0 EV = middle grey). Lets the artist see whether auto-exposure
        // is actually doing anything when toggled on.
        const float lum = ec.renderSystem.getBackend().getAdaptedLuminance();
        const float ev  = std::log2(std::max(lum / 0.18f, 1e-4f));
        ImGui::TextDisabled("Adapted: %.3f lum  (%+0.2f EV)", lum, ev);

        changed |= ImGui::Checkbox("Auto Exposure (eye adaptation)", &env.exposure.autoExposure);
        ImGui::BeginDisabled(!env.exposure.autoExposure);
        changed |= sliderF("Key", "##ExpKey", &env.exposure.key, 0.01f, 1.0f, "%.3f",
                "Target middle-grey the scene adapts toward");
        changed |= sliderF("Brighten Speed", "##ExpSpdUp",
                &env.exposure.speedBrighten, 0.05f, 10.0f, "%.2f",
                "Adaptation rate when the scene gets brighter (pupil constricts fast)");
        changed |= sliderF("Darken Speed", "##ExpSpdDn",
                &env.exposure.speedDarken, 0.05f, 10.0f, "%.2f",
                "Adaptation rate when the scene gets darker (eye rods recover slowly)");
        drawPropertyLabel("Min / Max");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::DragFloatRange2("##ExpRange", &env.exposure.min, &env.exposure.max,
            0.01f, 0.001f, 32.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Clamp on the auto-derived exposure");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardWithEnable("dof", "Depth of Field", &env.dof.enabled)) {
        ImGui::BeginDisabled(!env.dof.enabled);
        changed |= sliderF("Focus Distance", "##DofDist", &env.dof.focusDistance, 0.1f, 200.0f,
                "%.1f", "View-space distance kept sharp", true);
        changed |= sliderF("Focus Range", "##DofRange", &env.dof.focusRange, 0.1f, 200.0f,
                "%.1f", "Depth around the focus that stays sharp", true);
        changed |= sliderF("Max Blur", "##DofBlur", &env.dof.maxBlur, 0.0f, 0.1f, "%.3f",
                "Largest gather radius (UV)");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardWithEnable("mb", "Motion Blur (camera)", &env.motionBlur.enabled)) {
        ImGui::BeginDisabled(!env.motionBlur.enabled);
        changed |= sliderF("Strength", "##MbStr", &env.motionBlur.strength, 0.0f, 4.0f, "%.2f",
                "Camera reprojection blur amount");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardWithEnable("taa", "Temporal AA", &env.taa.enabled)) {
        ImGui::BeginDisabled(!env.taa.enabled);
        changed |= sliderF("History Blend", "##TaaBlend", &env.taa.blend, 0.0f, 0.98f, "%.3f",
                "History weight (MSAA already does spatial edge AA)");
        ImGui::EndDisabled();
    }
    return changed;
}

bool EnvironmentInspector::drawImagePost(EditorContext& /*ec*/, EnvironmentConfig& env) {
    bool changed = false;
    auto cardWithEnable = [&](const char* id, const char* title, bool* enabled) {
        const bool prev = *enabled;
        const bool open = cardHeader(id, title, enabled);
        if (*enabled != prev) changed = true;
        return open;
    };

    bool       bloomOn   = env.bloom.strength > 0.0001f;
    const bool bloomPrev = bloomOn;
    const bool bloomOpen = cardHeader("bloom", "Bloom", &bloomOn);
    if (bloomOn != bloomPrev) {
        if (bloomOn) {
            env.bloom.strength = m_bloomStrengthMemo > 0.0001f
                ? m_bloomStrengthMemo : 0.04f;
        } else {
            m_bloomStrengthMemo = env.bloom.strength;
            env.bloom.strength = 0.0f;
        }
        changed = true;
    }
    if (bloomOpen) {
        ImGui::BeginDisabled(!bloomOn);
        if (sliderF("Strength", "##BloomStr", &env.bloom.strength, 0.0f, 0.3f, "%.3f",
                "Linear-HDR bloom blended before exposure + AgX")) {
            if (env.bloom.strength > 0.0001f) m_bloomStrengthMemo = env.bloom.strength;
            changed = true;
        }
        changed |= sliderF("Threshold", "##BloomThr", &env.bloom.threshold, 0.0f, 4.0f, "%.2f",
                "Brightness below which a pixel contributes nothing. 0 = all light blooms.");
        changed |= sliderF("Knee", "##BloomKnee", &env.bloom.knee, 0.0f, 1.0f, "%.2f",
                "Soft-knee half-width around threshold. 0 = hard cutoff.");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardWithEnable("lensdirt", "Lens Dirt", &env.lensDirt.enabled)) {
        ImGui::BeginDisabled(!env.lensDirt.enabled);
        changed |= sliderF("Intensity", "##DirtInt", &env.lensDirt.intensity, 0.0f, 2.0f, "%.2f",
                "Dust spots light up wherever bloom is bright. Needs bloom on.");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardWithEnable("lensflare", "Lens Flare", &env.lensFlare.enabled)) {
        ImGui::BeginDisabled(!env.lensFlare.enabled);
        changed |= sliderF("Intensity", "##LFInt", &env.lensFlare.intensity, 0.0f, 4.0f, "%.2f",
                "Overall flare gain");
        changed |= sliderF("Threshold", "##LFThr", &env.lensFlare.threshold, 0.0f, 8.0f, "%.2f",
                "HDR luminance floor for contributing pixels");
        drawPropertyLabel("Ghosts");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::SliderInt("##LFGhosts", &env.lensFlare.ghostCount, 1, 8);
        changed |= sliderF("Ghost Spacing", "##LFSpace", &env.lensFlare.ghostSpacing, 0.05f, 1.0f, "%.2f",
                "UV step between ghosts along the optical axis");
        changed |= sliderF("Halo Radius", "##LFHalo", &env.lensFlare.haloRadius, 0.0f, 0.6f, "%.2f",
                "UV distance from source to halo ring");
        changed |= sliderF("Chromatic", "##LFChrom", &env.lensFlare.chromatic, 0.0f, 0.04f, "%.3f",
                "Per-channel UV offset for rainbow fringe");

        ImGui::Spacing();
        changed |= ImGui::Checkbox("Starburst", &env.lensFlare.starburst.enabled);
        if (env.lensFlare.starburst.enabled) {
            changed |= sliderF("Star Intensity", "##StarInt", &env.lensFlare.starburst.intensity, 0.0f, 4.0f, "%.2f",
                    "Aperture-blade spokes multiplied onto the halo");
        }
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardWithEnable("cg", "Color Grading (LUT)", &env.colorGrade.enabled)) {
        ImGui::BeginDisabled(!env.colorGrade.enabled);
        if (env.colorGrade.lutPath != m_lutPathLastSync) {
            snprintf(m_lutPathBuf, sizeof(m_lutPathBuf), "%s", env.colorGrade.lutPath.c_str());
            m_lutPathLastSync = env.colorGrade.lutPath;
        }
        drawPropertyLabel("LUT strip");
        ImGui::SetNextItemWidth(-150.0f);
        if (ImGui::InputText("##LutPath", m_lutPathBuf, sizeof(m_lutPathBuf),
                ImGuiInputTextFlags_EnterReturnsTrue)) {
            env.colorGrade.lutPath = m_lutPathBuf;
            m_lutPathLastSync = env.colorGrade.lutPath;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply##LUT")) {
            env.colorGrade.lutPath = m_lutPathBuf;
            m_lutPathLastSync = env.colorGrade.lutPath;
            changed = true;
        }
        ImGui::SameLine();
        {
            const std::string prev = env.colorGrade.lutPath;
            browseButton("Browse##LUT", m_lutPicker, "assets/lut",
                         {".png", ".PNG", ".cube", ".CUBE"}, env.colorGrade.lutPath);
            if (env.colorGrade.lutPath != prev) changed = true;
        }
        changed |= sliderF("Intensity", "##LutInt", &env.colorGrade.intensity, 0.0f, 1.0f, "%.2f",
                "Blend toward the graded look");

        // Lazy-load the thumbnail when the active path changes. The composite
        // pass loads its own copy for sampling; this is a separate, smaller
        // one that lives on the panel so the artist can confirm at a glance
        // that they picked the right strip without flipping back to the
        // viewport. m_lutThumbPath is recorded on failure too so a bad path
        // doesn't re-attempt the read every frame.
        if (env.colorGrade.lutPath != m_lutThumbPath) {
            m_lutThumb.reset();
            if (!env.colorGrade.lutPath.empty()) {
                LDRImage img = loadColorLUT(env.colorGrade.lutPath);
                if (img.valid()) {
                    Core::Texture2DParams p;
                    p.width           = img.width;
                    p.height          = img.height;
                    p.internalFormat  = GL_RGBA8;
                    p.format          = GL_RGBA;
                    p.type            = GL_UNSIGNED_BYTE;
                    p.wrapS           = Core::TextureWrap::ClampToEdge;
                    p.wrapT           = Core::TextureWrap::ClampToEdge;
                    p.minFilter       = Core::TextureMinFilter::Linear;
                    p.magFilter       = Core::TextureMagFilter::Linear;
                    p.generateMipmaps = false;
                    p.data            = img.pixels.data();
                    m_lutThumb = std::make_unique<Core::Texture2D>("lut_thumb", p);
                }
            }
            m_lutThumbPath = env.colorGrade.lutPath;
        }
        if (m_lutThumb) {
            // LUT strips are very wide (typically 1024x32). Display at the
            // available width with proportional height so the strip reads as
            // a colour bar; the user verifies the LUT is what they think it
            // is from the dominant hues + the gradient shape.
            const float w = ImGui::GetContentRegionAvail().x;
            const float h = w * static_cast<float>(m_lutThumb->getHeight())
                              / static_cast<float>(std::max(m_lutThumb->getWidth(), 1u));
            ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(m_lutThumb->getID())),
                         ImVec2(w, h));
        } else if (!env.colorGrade.lutPath.empty()) {
            ImGui::TextDisabled("Could not load %s",
                std::filesystem::path(env.colorGrade.lutPath).filename().string().c_str());
        }
        ImGui::EndDisabled();
    }
    return changed;
}

bool EnvironmentInspector::drawScreenSpaceFX(EditorContext& /*ec*/, EnvironmentConfig& env) {
    bool changed = false;
    auto cardWithEnable = [&](const char* id, const char* title, bool* enabled) {
        const bool prev = *enabled;
        const bool open = cardHeader(id, title, enabled);
        if (*enabled != prev) changed = true;
        return open;
    };

    if (cardWithEnable("ssao", "Ambient Occlusion (GTAO)", &env.ao.enabled)) {
        ImGui::BeginDisabled(!env.ao.enabled);
        changed |= sliderF("Radius", "##AoRad", &env.ao.radius, 0.05f, 5.0f, "%.2f",
                "View-space sampling radius");
        changed |= sliderF("Intensity", "##AoInt", &env.ao.intensity, 0.0f, 4.0f, "%.2f",
                "Occlusion darkening strength");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardWithEnable("ssr", "Screen-Space Reflections", &env.ssr.enabled)) {
        ImGui::BeginDisabled(!env.ssr.enabled);
        changed |= sliderF("Intensity", "##SsrInt", &env.ssr.intensity, 0.0f, 2.0f, "%.2f",
                "Reflection blend strength");
        changed |= sliderF("Max Distance", "##SsrDist", &env.ssr.maxDistance, 0.5f, 50.0f, "%.1f",
                "View-space ray length");
        changed |= sliderF("Thickness", "##SsrThick", &env.ssr.thickness, 0.02f, 4.0f, "%.2f",
                "Depth hit tolerance");
        ImGui::EndDisabled();
    }

    return changed;
}

bool EnvironmentInspector::drawDiagnostics(EditorContext& /*ec*/, EnvironmentConfig& env) {
    bool changed = false;
    auto cardWithEnable = [&](const char* id, const char* title, bool* enabled) {
        const bool prev = *enabled;
        const bool open = cardHeader(id, title, enabled);
        if (*enabled != prev) changed = true;
        return open;
    };

    // Render mode (Default + 7 diagnostic views). The forward pass + post
    // chain key off view.modeConfig derived from this each frame; each
    // diagnostic mode either routes geometry through a different shader
    // (Unlit, Wireframe), runs a second wireframe draw on top of the shaded
    // result (WireframeOverShaded), overrides the material before lighting
    // (LightingOnly), or branches at the end of the PBR fragment shader to
    // write a diagnostic value (Normals, Depth, AOOnly). Most modes bypass
    // the post chain + display transform so what you see is what the shader
    // wrote.
    drawPropertyLabel("Render Mode");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= drawRenderModeCombo("##RenderMode", env.renderMode);
    ImGui::Spacing();

    if (cardWithEnable("grid", "Grid", &env.grid.enabled)) {
        ImGui::BeginDisabled(!env.grid.enabled);
        changed |= sliderF("Cell Size", "##GScale", &env.grid.scale, 0.1f, 100.0f, "%.1f",
                "World units per grid cell");
        drawPropertyLabel("Extent");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::DragFloat("##GSize", &env.grid.size, 10.0f, 10.0f, 10000.0f, "%.0f");
        changed |= sliderF("Fade Start", "##GFadeS", &env.grid.fadeStart, 1.0f,
                env.grid.fadeEnd, "%.0f", "Distance the grid begins to fade");
        changed |= sliderF("Fade End", "##GFadeE", &env.grid.fadeEnd, env.grid.fadeStart,
                10000.0f, "%.0f", "Distance the grid fully fades", true);
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardWithEnable("aabb", "AABB Debug", &env.aabbDebug.enabled)) {
        ImGui::BeginDisabled(!env.aabbDebug.enabled);
        drawPropertyLabel("Box Color");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::ColorEdit3("##AABBCol", glm::value_ptr(env.aabbDebug.color),
            ImGuiColorEditFlags_Float);
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardWithEnable("selection", "Selection Outline", &env.selection.enabled)) {
        ImGui::BeginDisabled(!env.selection.enabled);
        drawPropertyLabel("Color");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::ColorEdit3("##SelCol", glm::value_ptr(env.selection.color),
            ImGuiColorEditFlags_Float);
        changed |= sliderF("Thickness", "##SelThk", &env.selection.thickness,
            0.5f, 8.0f, "%.1f px",
            "Silhouette outline width in screen pixels");
        ImGui::EndDisabled();
    }
    return changed;
}

bool EnvironmentInspector::drawPerformance(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    bool changed = false;

    if (cardHeader("passes", "Render Passes", nullptr)) {
        ImGui::TextDisabled("Per-pass enable/disable (advanced).");
        ImGui::Spacing();
        // Narrow API on RenderSystem - the panel doesn't include render_graph.h
        // or know about the pass class hierarchy.
        for (size_t i = 0; i < ec.renderSystem.passCount(); ++i) {
            bool enabled = ec.renderSystem.isPassEnabled(i);
            const std::string_view name = ec.renderSystem.passName(i);
            // ImGui::Checkbox needs a C string; passName is null-terminated
            // (RenderPass::getName returns const std::string&) so .data()
            // is safe here.
            if (ImGui::Checkbox(name.data(), &enabled)) {
                ec.renderSystem.setPassEnabled(i, enabled);
                changed = true;
            }
        }
    }

    ImGui::Spacing();
    if (cardHeader("vis", "Visibility Culling", nullptr)) {
        auto& settings = ec.visibilitySystem.getSettings();
        changed |= sliderF("Min Pixels", "##MinPx", &settings.minPixels, 0.0f, 100.0f, "%.1f",
                "Skip objects smaller than this on screen");
        drawPropertyLabel("Max Distance");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::DragFloat("##MaxD", &settings.maxDistance, 1.0f, 10.0f, 10000.0f, "%.0f");
        if (ctx.visibility) {
            size_t vis = ctx.visibility->entries.size();
            size_t tot = ctx.scene.entityCount();
            ImGui::TextDisabled("Culled: %zu / %zu", tot > vis ? tot - vis : 0, tot);
        }
    }
    return changed;
}

void EnvironmentInspector::draw(EditorContext& ec, EnvironmentConfig& env) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##EnvFilter", "Search settings...",
                             m_filter, sizeof(m_filter));
    ImGui::PopStyleVar();
    ImGui::Spacing();

    bool changed = drawPresetBar(env);
    ImGui::Spacing();

    const bool filtering = m_filter[0] != '\0';

    // Each group is a real Inspector component card (colored accent rail +
    // guide line). When searching, matching cards are force-opened. Card
    // order intentionally moves outward: World (background of the scene) ->
    // Lighting & Shadows (geometric light setup) -> Camera FX (what the
    // camera does to incoming light) -> Image Post / Screen-Space FX
    // (image-finishing) -> Diagnostics (debug overlays) -> Performance
    // (perf-only knobs, advanced + collapsed).
    auto card = [&](const char* title, const ImVec4& accent, bool openByDefault,
                    bool (EnvironmentInspector::*body)(EditorContext&, EnvironmentConfig&)) {
        if (filtering) {
            if (!matchesFilter(title, m_filter)) return;
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        if (beginComponentCard(title, accent, openByDefault))
            changed |= (this->*body)(ec, env);
        endComponentCard();
    };

    card("World",              ACCENT_WORLD,    true,  &EnvironmentInspector::drawWorld);
    card("Lighting & Shadows", ACCENT_LIGHTING, true,  &EnvironmentInspector::drawLightingShadows);
    card("Camera FX",          ACCENT_CAMERA,   true,  &EnvironmentInspector::drawCameraFX);
    card("Image Post",         ACCENT_POST,     true,  &EnvironmentInspector::drawImagePost);
    card("Screen-Space FX",    ACCENT_SSFX,     true,  &EnvironmentInspector::drawScreenSpaceFX);
    card("Diagnostics",        ACCENT_DIAG,     false, &EnvironmentInspector::drawDiagnostics);

    // Performance takes only the context; advanced, collapsed by default.
    if (!filtering || matchesFilter("Performance (advanced)", m_filter)) {
        if (filtering) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (beginComponentCard("Performance (advanced)", ACCENT_PERF, false))
            changed |= drawPerformance(ec);
        endComponentCard();
    }

    if (changed) ec.state.markSceneDirty();
}

} // namespace Engine
