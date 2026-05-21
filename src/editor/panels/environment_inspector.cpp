#include "panels/environment_inspector.h"
#include "framework/editor_common.h"
#include "ui/editor_style.h"

#include "system/render/render_view.h"          // EnvironmentConfig
#include "system/visibility/visibility.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "system/render/render_graph.h"
#include "system/render/render_pass.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <filesystem>
#include <system_error>
#include <initializer_list>

#include <glm/gtc/type_ptr.hpp>

namespace Engine {

namespace {

    // Per-group accent rail colors - same palette the Inspector uses for
    // Transform/Light/Camera so the Environment reads as part of the editor,
    // not a bolted-on panel.
    const ImVec4 ACCENT_LIGHTING = ImVec4(1.00f, 0.80f, 0.22f, 1.0f);  // gold
    const ImVec4 ACCENT_CAMERA   = ImVec4(0.30f, 0.78f, 0.80f, 1.0f);  // cyan
    const ImVec4 ACCENT_POST     = EditorStyle::ACCENT;                // blue
    const ImVec4 ACCENT_SCENE    = ImVec4(0.55f, 0.58f, 0.62f, 1.0f);  // gray
    const ImVec4 ACCENT_PIPE     = ImVec4(0.64f, 0.44f, 0.86f, 1.0f);  // purple

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
    bool sliderF(const char* label, const char* id, float* v,
                 float lo, float hi, const char* fmt, const char* tip,
                 bool logarithmic = false) {
        drawPropertyLabel(label);
        ImGui::SetNextItemWidth(-1.0f);
        bool ch = ImGui::SliderFloat(id, v, lo, hi, fmt,
            logarithmic ? ImGuiSliderFlags_Logarithmic : 0);
        if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        return ch;
    }

    // "Browse" button + AssetPicker modal. Picker is owned by the panel
    // (one per slot) so its on-open scan cache outlives a single draw.
    void browseButton(const char* btnLabel, AssetPicker& picker,
                      const char* subdir,
                      std::initializer_list<const char*> exts,
                      std::string& target) {
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

void EnvironmentInspector::drawPresetBar(EnvironmentConfig& env) {
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
        if (ImGui::Button(PRESET_NAMES[i], ImVec2(segW, h)))
            applyPreset(env, static_cast<Preset>(i));
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
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Restore documented defaults (keeps the HDR / LUT paths)");
    ImGui::PopStyleVar(2);
}

void EnvironmentInspector::drawLighting(EditorContext& /*ec*/, EnvironmentConfig& env) {
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
    }
    if (iblOpen) {
        ImGui::BeginDisabled(!iblOn);
        snprintf(m_hdrPathBuf, sizeof(m_hdrPathBuf), "%s", env.ibl.path.c_str());
        drawPropertyLabel("HDR Path");
        ImGui::SetNextItemWidth(-150.0f);
        if (ImGui::InputText("##IBLPath", m_hdrPathBuf, sizeof(m_hdrPathBuf),
                ImGuiInputTextFlags_EnterReturnsTrue))
            env.ibl.path = m_hdrPathBuf;
        ImGui::SameLine();
        if (ImGui::Button("Apply##IBL")) env.ibl.path = m_hdrPathBuf;
        ImGui::SameLine();
        browseButton("Browse##IBL", m_iblPicker, "assets/envs",
                     {".hdr", ".HDR"}, env.ibl.path);
        sliderF("Intensity", "##IBLInt", &env.ibl.intensity, 0.0f, 5.0f, "%.2f",
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
        ImGui::ColorEdit3("##AmbCol", glm::value_ptr(env.ambient.color),
            ImGuiColorEditFlags_Float);
        sliderF("Intensity", "##AmbInt", &env.ambient.intensity, 0.0f, 2.0f, "%.3f",
                "Flat ambient used when no IBL map is set");
    }

    ImGui::Spacing();
    if (cardHeader("bg", "Background", nullptr)) {
        drawPropertyLabel("Clear Color");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::ColorEdit3("##ClearCol", glm::value_ptr(env.clearColor),
            ImGuiColorEditFlags_Float);
    }
}

void EnvironmentInspector::drawCamera(EditorContext& ec, EnvironmentConfig& env) {
    FrameContext& ctx = ec.frame;

    if (cardHeader("exp", "Exposure", nullptr)) {
        drawPropertyLabel("Tone Mapping");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##Tonemap", &env.tonemap,
            "AgX (filmic)\0PBR Neutral (albedo-faithful)\0ACES\0Reinhard\0");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Display transform. PBR Neutral preserves material color\n"
                "(matches online glTF viewers); AgX is a desaturating film look.");

        const float camExp = (ctx.visibility && ctx.visibility->hasCamera)
            ? ctx.visibility->cameraExposure : 1.0f;
        ImGui::TextDisabled("Manual camera exposure: %.2f (edit on the Camera entity)",
            camExp);
        ImGui::Checkbox("Auto Exposure (eye adaptation)", &env.exposure.autoExposure);
        ImGui::BeginDisabled(!env.exposure.autoExposure);
        sliderF("Key", "##ExpKey", &env.exposure.key, 0.01f, 1.0f, "%.3f",
                "Target middle-grey the scene adapts toward");
        sliderF("Adapt Speed", "##ExpSpd", &env.exposure.speed, 0.05f, 10.0f, "%.2f",
                "How fast the eye adapts (per second)");
        drawPropertyLabel("Min / Max");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloatRange2("##ExpRange", &env.exposure.min, &env.exposure.max,
            0.01f, 0.001f, 32.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Clamp on the auto-derived exposure");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("dof", "Depth of Field", &env.dof.enabled)) {
        ImGui::BeginDisabled(!env.dof.enabled);
        sliderF("Focus Distance", "##DofDist", &env.dof.focusDistance, 0.1f, 200.0f,
                "%.1f", "View-space distance kept sharp", true);
        sliderF("Focus Range", "##DofRange", &env.dof.focusRange, 0.1f, 200.0f,
                "%.1f", "Depth around the focus that stays sharp", true);
        sliderF("Max Blur", "##DofBlur", &env.dof.maxBlur, 0.0f, 0.1f, "%.3f",
                "Largest gather radius (UV)");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("mb", "Motion Blur (camera)", &env.motionBlur.enabled)) {
        ImGui::BeginDisabled(!env.motionBlur.enabled);
        sliderF("Strength", "##MbStr", &env.motionBlur.strength, 0.0f, 4.0f, "%.2f",
                "Camera reprojection blur amount");
        ImGui::EndDisabled();
    }
}

void EnvironmentInspector::drawPost(EditorContext& /*ec*/, EnvironmentConfig& env) {
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
    }
    if (bloomOpen) {
        ImGui::BeginDisabled(!bloomOn);
        if (sliderF("Strength", "##BloomStr", &env.bloom.strength, 0.0f, 0.3f, "%.3f",
                "Linear-HDR bloom blended before exposure + AgX")
            && env.bloom.strength > 0.0001f)
            m_bloomStrengthMemo = env.bloom.strength;
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("lensdirt", "Lens Dirt", &env.lensDirt.enabled)) {
        ImGui::BeginDisabled(!env.lensDirt.enabled);
        sliderF("Intensity", "##DirtInt", &env.lensDirt.intensity, 0.0f, 2.0f, "%.2f",
                "Dust spots light up wherever bloom is bright. Needs bloom on.");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("lensflare", "Lens Flare", &env.lensFlare.enabled)) {
        ImGui::BeginDisabled(!env.lensFlare.enabled);
        sliderF("Intensity", "##LFInt", &env.lensFlare.intensity, 0.0f, 4.0f, "%.2f",
                "Overall flare gain");
        sliderF("Threshold", "##LFThr", &env.lensFlare.threshold, 0.0f, 8.0f, "%.2f",
                "HDR luminance floor for contributing pixels");
        drawPropertyLabel("Ghosts");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##LFGhosts", &env.lensFlare.ghostCount, 1, 8);
        sliderF("Ghost Spacing", "##LFSpace", &env.lensFlare.ghostSpacing, 0.05f, 1.0f, "%.2f",
                "UV step between ghosts along the optical axis");
        sliderF("Halo Radius", "##LFHalo", &env.lensFlare.haloRadius, 0.0f, 0.6f, "%.2f",
                "UV distance from source to halo ring");
        sliderF("Chromatic", "##LFChrom", &env.lensFlare.chromatic, 0.0f, 0.04f, "%.3f",
                "Per-channel UV offset for rainbow fringe");

        ImGui::Spacing();
        ImGui::Checkbox("Starburst", &env.lensFlare.starburst.enabled);
        if (env.lensFlare.starburst.enabled) {
            sliderF("Star Intensity", "##StarInt", &env.lensFlare.starburst.intensity, 0.0f, 4.0f, "%.2f",
                    "Aperture-blade spokes multiplied onto the halo");
        }
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("ssao", "Ambient Occlusion (GTAO)", &env.ao.enabled)) {
        ImGui::BeginDisabled(!env.ao.enabled);
        sliderF("Radius", "##AoRad", &env.ao.radius, 0.05f, 5.0f, "%.2f",
                "View-space sampling radius");
        sliderF("Intensity", "##AoInt", &env.ao.intensity, 0.0f, 4.0f, "%.2f",
                "Occlusion darkening strength");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("ssr", "Screen-Space Reflections", &env.ssr.enabled)) {
        ImGui::BeginDisabled(!env.ssr.enabled);
        sliderF("Intensity", "##SsrInt", &env.ssr.intensity, 0.0f, 2.0f, "%.2f",
                "Reflection blend strength");
        sliderF("Max Distance", "##SsrDist", &env.ssr.maxDistance, 0.5f, 50.0f, "%.1f",
                "View-space ray length");
        sliderF("Thickness", "##SsrThick", &env.ssr.thickness, 0.02f, 4.0f, "%.2f",
                "Depth hit tolerance");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("taa", "Temporal AA", &env.taa.enabled)) {
        ImGui::BeginDisabled(!env.taa.enabled);
        sliderF("History Blend", "##TaaBlend", &env.taa.blend, 0.0f, 0.98f, "%.3f",
                "History weight (MSAA already does spatial edge AA)");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("cg", "Color Grading (LUT)", &env.colorGrade.enabled)) {
        ImGui::BeginDisabled(!env.colorGrade.enabled);
        snprintf(m_lutPathBuf, sizeof(m_lutPathBuf), "%s", env.colorGrade.lutPath.c_str());
        drawPropertyLabel("LUT strip");
        ImGui::SetNextItemWidth(-150.0f);
        if (ImGui::InputText("##LutPath", m_lutPathBuf, sizeof(m_lutPathBuf),
                ImGuiInputTextFlags_EnterReturnsTrue))
            env.colorGrade.lutPath = m_lutPathBuf;
        ImGui::SameLine();
        if (ImGui::Button("Apply##LUT")) env.colorGrade.lutPath = m_lutPathBuf;
        ImGui::SameLine();
        browseButton("Browse##LUT", m_lutPicker, "assets/lut",
                     {".png", ".PNG"}, env.colorGrade.lutPath);
        sliderF("Intensity", "##LutInt", &env.colorGrade.intensity, 0.0f, 1.0f, "%.2f",
                "Blend toward the graded look");
        if (!env.colorGrade.lutPath.empty())
            ImGui::TextDisabled("%s",
                std::filesystem::path(env.colorGrade.lutPath).filename().string().c_str());
        ImGui::EndDisabled();
    }
}

void EnvironmentInspector::drawScene(EditorContext& /*ec*/, EnvironmentConfig& env) {
    // Render mode (Default / Wireframe / future debug views). The forward
    // pass + post chain key off view.modeConfig derived from this each
    // frame; selecting Wireframe routes geometry through the unlit shader,
    // disables post-effects, and bypasses the display transform.
    {
        drawPropertyLabel("Render Mode");
        ImGui::SetNextItemWidth(-1.0f);
        static const char* MODE_NAMES[] = { "Default", "Wireframe" };
        int modeIdx = static_cast<int>(env.renderMode);
        if (ImGui::Combo("##RenderMode", &modeIdx, MODE_NAMES, IM_ARRAYSIZE(MODE_NAMES))) {
            env.renderMode = static_cast<RenderMode>(modeIdx);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Diagnostic view selector. Wireframe shows unlit "
                              "geometry edges with post-effects + tonemap bypassed.");
        }
        ImGui::Spacing();
    }

    if (cardHeader("grid", "Grid", &env.grid.enabled)) {
        ImGui::BeginDisabled(!env.grid.enabled);
        sliderF("Cell Size", "##GScale", &env.grid.scale, 0.1f, 100.0f, "%.1f",
                "World units per grid cell");
        drawPropertyLabel("Extent");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##GSize", &env.grid.size, 10.0f, 10.0f, 10000.0f, "%.0f");
        sliderF("Fade Start", "##GFadeS", &env.grid.fadeStart, 1.0f,
                env.grid.fadeEnd, "%.0f", "Distance the grid begins to fade");
        sliderF("Fade End", "##GFadeE", &env.grid.fadeEnd, env.grid.fadeStart,
                10000.0f, "%.0f", "Distance the grid fully fades", true);
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("aabb", "AABB Debug", &env.aabbDebug.enabled)) {
        ImGui::BeginDisabled(!env.aabbDebug.enabled);
        drawPropertyLabel("Box Color");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::ColorEdit3("##AABBCol", glm::value_ptr(env.aabbDebug.color),
            ImGuiColorEditFlags_Float);
        ImGui::EndDisabled();
    }
}

void EnvironmentInspector::drawPipeline(EditorContext& ec) {
    FrameContext& ctx = ec.frame;

    ImGui::TextDisabled("Toggle individual graph passes (advanced).");
    ImGui::Spacing();
    // Narrow API on RenderSystem - the panel doesn't include render_graph.h
    // or know about the pass class hierarchy.
    for (size_t i = 0; i < ec.renderSystem.passCount(); ++i) {
        bool enabled = ec.renderSystem.isPassEnabled(i);
        const std::string_view name = ec.renderSystem.passName(i);
        // ImGui::Checkbox needs a C string; passName is null-terminated
        // (RenderPass::getName returns const std::string&) so .data()
        // is safe here.
        if (ImGui::Checkbox(name.data(), &enabled))
            ec.renderSystem.setPassEnabled(i, enabled);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Visibility Culling");
    {
        auto& settings = ec.visibilitySystem.getSettings();
        sliderF("Min Pixels", "##MinPx", &settings.minPixels, 0.0f, 100.0f, "%.1f",
                "Skip objects smaller than this on screen");
        drawPropertyLabel("Max Distance");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##MaxD", &settings.maxDistance, 1.0f, 10.0f, 10000.0f, "%.0f");
        if (ctx.visibility) {
            size_t vis = ctx.visibility->entries.size();
            size_t tot = ctx.scene.entityCount();
            ImGui::TextDisabled("Culled: %zu / %zu", tot > vis ? tot - vis : 0, tot);
        }
    }
}

void EnvironmentInspector::draw(EditorContext& ec, EnvironmentConfig& env) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##EnvFilter", "Search settings...",
                             m_filter, sizeof(m_filter));
    ImGui::PopStyleVar();
    ImGui::Spacing();

    drawPresetBar(env);
    ImGui::Spacing();

    const bool filtering = m_filter[0] != '\0';

    // Each group is a real Inspector component card (colored accent rail +
    // guide line). When searching, matching cards are force-opened.
    auto card = [&](const char* title, const ImVec4& accent, bool openByDefault,
                    void (EnvironmentInspector::*body)(EditorContext&, EnvironmentConfig&)) {
        if (filtering) {
            if (!matchesFilter(title, m_filter)) return;
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        if (beginComponentCard(title, accent, openByDefault))
            (this->*body)(ec, env);
        endComponentCard();
    };

    card("Lighting",          ACCENT_LIGHTING, true, &EnvironmentInspector::drawLighting);
    card("Camera & Exposure", ACCENT_CAMERA,   true, &EnvironmentInspector::drawCamera);
    card("Post-Processing",   ACCENT_POST,     true, &EnvironmentInspector::drawPost);
    card("Scene & Debug",     ACCENT_SCENE,    true, &EnvironmentInspector::drawScene);

    // Pipeline takes only the context; advanced, collapsed by default.
    if (!filtering || matchesFilter("Pipeline (advanced)", m_filter)) {
        if (filtering) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (beginComponentCard("Pipeline (advanced)", ACCENT_PIPE, false))
            drawPipeline(ec);
        endComponentCard();
    }
}

} // namespace Engine
