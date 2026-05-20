#include "panels/environment_inspector.h"
#include "framework/editor_common.h"
#include "ui/editor_style.h"

#include "system/render/render_view.h"          // EnvironmentConfig
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "system/render/render_graph.h"

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

    // "Browse" button + modal listing APP_ROOT_DIR/<subdir> filtered by
    // extension; picking a file writes a root-relative path into @p target.
    void fileBrowse(const char* id, const char* subdir,
                    std::initializer_list<const char*> exts,
                    std::string& target) {
        char popupId[64];
        snprintf(popupId, sizeof(popupId), "Browse##%s", id);
        if (ImGui::Button(popupId)) ImGui::OpenPopup(popupId);
        if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::filesystem::path root = std::filesystem::path(APP_ROOT_DIR) / subdir;
            ImGui::TextDisabled("%s", root.string().c_str());
            ImGui::Separator();
            std::error_code ec;
            bool any = false;
            for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
                if (!e.is_regular_file()) continue;
                const std::string ext = e.path().extension().string();
                bool match = false;
                for (const char* x : exts) if (ext == x) { match = true; break; }
                if (!match) continue;
                any = true;
                const std::string name = e.path().filename().string();
                if (ImGui::Selectable(name.c_str())) {
                    target = std::string(subdir) + "/" + name;
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!any) ImGui::TextDisabled("(no matching files here)");
            ImGui::Separator();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    enum class Preset { Low, Medium, High, Cinematic };

    // Presets flip effect enables + bloom amount + auto-exposure (predictable;
    // they do not clobber paths, colours, grid or exposure tuning). Auto
    // exposure is on for the higher tiers (Low/Medium stay reference-faithful).
    void applyPreset(EnvironmentConfig& env, Preset p) {
        switch (p) {
            case Preset::Low:
                env.ssao = false; env.ssr = false; env.taa = false;
                env.dof = false; env.motionBlur = false;
                env.bloomStrength = 0.0f;
                env.autoExposure = false;
                break;
            case Preset::Medium:
                env.ssao = true;  env.ssr = false; env.taa = false;
                env.dof = false; env.motionBlur = false;
                env.bloomStrength = 0.03f;
                env.autoExposure = false;
                break;
            case Preset::High:
                env.ssao = true;  env.ssr = true;  env.taa = false;
                env.dof = false; env.motionBlur = false;
                env.bloomStrength = 0.04f;
                env.autoExposure = true;
                break;
            case Preset::Cinematic:
                env.ssao = true;  env.ssr = true;  env.taa = true;
                env.dof = true;  env.motionBlur = true;
                env.bloomStrength = 0.06f;
                env.autoExposure = true;
                break;
        }
    }

    // Which preset (if any) the current env exactly matches. -1 = Custom.
    int detectPreset(const EnvironmentConfig& env) {
        auto matches = [&](bool ssao, bool ssr, bool taa, bool dof,
                           bool mb, float bloom, bool ae) {
            float d = env.bloomStrength - bloom;
            if (d < 0.0f) d = -d;
            return env.ssao == ssao && env.ssr == ssr && env.taa == taa &&
                   env.dof == dof && env.motionBlur == mb &&
                   env.autoExposure == ae && d < 5e-4f;
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
        const std::string ep = env.environmentMapPath;
        const std::string lp = env.colorLutPath;
        env = EnvironmentConfig{};
        env.environmentMapPath = ep;   // keep the asset references
        env.colorLutPath       = lp;
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Restore documented defaults (keeps the HDR / LUT paths)");
    ImGui::PopStyleVar(2);
}

void EnvironmentInspector::drawLighting(EditorContext& /*ec*/, EnvironmentConfig& env) {
    bool       iblOn   = !env.environmentMapPath.empty();
    const bool iblPrev = iblOn;
    const bool iblOpen = cardHeader("ibl", "Environment Map (IBL)", &iblOn);
    if (iblOn != iblPrev) {
        if (iblOn) {
            env.environmentMapPath = !m_iblPathMemo.empty()
                ? m_iblPathMemo : std::string("assets/envs/environment.hdr");
        } else {
            m_iblPathMemo = env.environmentMapPath;
            env.environmentMapPath.clear();
        }
    }
    if (iblOpen) {
        ImGui::BeginDisabled(!iblOn);
        static char hdrBuf[260];
        snprintf(hdrBuf, sizeof(hdrBuf), "%s", env.environmentMapPath.c_str());
        drawPropertyLabel("HDR Path");
        ImGui::SetNextItemWidth(-150.0f);
        if (ImGui::InputText("##IBLPath", hdrBuf, sizeof(hdrBuf),
                ImGuiInputTextFlags_EnterReturnsTrue))
            env.environmentMapPath = hdrBuf;
        ImGui::SameLine();
        if (ImGui::Button("Apply##IBL")) env.environmentMapPath = hdrBuf;
        ImGui::SameLine();
        fileBrowse("ibl", "assets/envs", {".hdr", ".HDR"}, env.environmentMapPath);
        sliderF("Intensity", "##IBLInt", &env.iblIntensity, 0.0f, 5.0f, "%.2f",
                "Strength of image-based ambient + specular");
        if (env.environmentMapPath.empty())
            ImGui::TextDisabled("No map - flat ambient fallback");
        else
            ImGui::TextDisabled("%s",
                std::filesystem::path(env.environmentMapPath).filename().string().c_str());
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("amb", "Ambient Light", nullptr)) {
        drawPropertyLabel("Color");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::ColorEdit3("##AmbCol", glm::value_ptr(env.ambientColor),
            ImGuiColorEditFlags_Float);
        sliderF("Intensity", "##AmbInt", &env.ambientIntensity, 0.0f, 2.0f, "%.3f",
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
        ImGui::Checkbox("Auto Exposure (eye adaptation)", &env.autoExposure);
        ImGui::BeginDisabled(!env.autoExposure);
        sliderF("Key", "##ExpKey", &env.exposureKey, 0.01f, 1.0f, "%.3f",
                "Target middle-grey the scene adapts toward");
        sliderF("Adapt Speed", "##ExpSpd", &env.exposureSpeed, 0.05f, 10.0f, "%.2f",
                "How fast the eye adapts (per second)");
        drawPropertyLabel("Min / Max");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloatRange2("##ExpRange", &env.exposureMin, &env.exposureMax,
            0.01f, 0.001f, 32.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Clamp on the auto-derived exposure");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("dof", "Depth of Field", &env.dof)) {
        ImGui::BeginDisabled(!env.dof);
        sliderF("Focus Distance", "##DofDist", &env.dofFocusDistance, 0.1f, 200.0f,
                "%.1f", "View-space distance kept sharp", true);
        sliderF("Focus Range", "##DofRange", &env.dofFocusRange, 0.1f, 200.0f,
                "%.1f", "Depth around the focus that stays sharp", true);
        sliderF("Max Blur", "##DofBlur", &env.dofMaxBlur, 0.0f, 0.1f, "%.3f",
                "Largest gather radius (UV)");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("mb", "Motion Blur (camera)", &env.motionBlur)) {
        ImGui::BeginDisabled(!env.motionBlur);
        sliderF("Strength", "##MbStr", &env.motionBlurStrength, 0.0f, 4.0f, "%.2f",
                "Camera reprojection blur amount");
        ImGui::EndDisabled();
    }
}

void EnvironmentInspector::drawPost(EditorContext& /*ec*/, EnvironmentConfig& env) {
    bool       bloomOn   = env.bloomStrength > 0.0001f;
    const bool bloomPrev = bloomOn;
    const bool bloomOpen = cardHeader("bloom", "Bloom", &bloomOn);
    if (bloomOn != bloomPrev) {
        if (bloomOn) {
            env.bloomStrength = m_bloomStrengthMemo > 0.0001f
                ? m_bloomStrengthMemo : 0.04f;
        } else {
            m_bloomStrengthMemo = env.bloomStrength;
            env.bloomStrength = 0.0f;
        }
    }
    if (bloomOpen) {
        ImGui::BeginDisabled(!bloomOn);
        if (sliderF("Strength", "##BloomStr", &env.bloomStrength, 0.0f, 0.3f, "%.3f",
                "Linear-HDR bloom blended before exposure + AgX")
            && env.bloomStrength > 0.0001f)
            m_bloomStrengthMemo = env.bloomStrength;
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("ssao", "Ambient Occlusion (GTAO)", &env.ssao)) {
        ImGui::BeginDisabled(!env.ssao);
        sliderF("Radius", "##AoRad", &env.ssaoRadius, 0.05f, 5.0f, "%.2f",
                "View-space sampling radius");
        sliderF("Intensity", "##AoInt", &env.ssaoIntensity, 0.0f, 4.0f, "%.2f",
                "Occlusion darkening strength");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("ssr", "Screen-Space Reflections", &env.ssr)) {
        ImGui::BeginDisabled(!env.ssr);
        sliderF("Intensity", "##SsrInt", &env.ssrIntensity, 0.0f, 2.0f, "%.2f",
                "Reflection blend strength");
        sliderF("Max Distance", "##SsrDist", &env.ssrMaxDistance, 0.5f, 50.0f, "%.1f",
                "View-space ray length");
        sliderF("Thickness", "##SsrThick", &env.ssrThickness, 0.02f, 4.0f, "%.2f",
                "Depth hit tolerance");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("taa", "Temporal AA", &env.taa)) {
        ImGui::BeginDisabled(!env.taa);
        sliderF("History Blend", "##TaaBlend", &env.taaBlend, 0.0f, 0.98f, "%.3f",
                "History weight (MSAA already does spatial edge AA)");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("cg", "Color Grading (LUT)", &env.colorGrade)) {
        ImGui::BeginDisabled(!env.colorGrade);
        static char lutBuf[260];
        snprintf(lutBuf, sizeof(lutBuf), "%s", env.colorLutPath.c_str());
        drawPropertyLabel("LUT strip");
        ImGui::SetNextItemWidth(-150.0f);
        if (ImGui::InputText("##LutPath", lutBuf, sizeof(lutBuf),
                ImGuiInputTextFlags_EnterReturnsTrue))
            env.colorLutPath = lutBuf;
        ImGui::SameLine();
        if (ImGui::Button("Apply##LUT")) env.colorLutPath = lutBuf;
        ImGui::SameLine();
        fileBrowse("lut", "assets/lut", {".png", ".PNG"}, env.colorLutPath);
        sliderF("Intensity", "##LutInt", &env.colorGradeIntensity, 0.0f, 1.0f, "%.2f",
                "Blend toward the graded look");
        if (!env.colorLutPath.empty())
            ImGui::TextDisabled("%s",
                std::filesystem::path(env.colorLutPath).filename().string().c_str());
        ImGui::EndDisabled();
    }
}

void EnvironmentInspector::drawScene(EditorContext& /*ec*/, EnvironmentConfig& env) {
    // Grid / AABB also have one-click toggles in the viewport "Show" menu.
    if (cardHeader("grid", "Grid", &env.gridEnabled)) {
        ImGui::BeginDisabled(!env.gridEnabled);
        sliderF("Cell Size", "##GScale", &env.gridScale, 0.1f, 100.0f, "%.1f",
                "World units per grid cell");
        drawPropertyLabel("Extent");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##GSize", &env.gridSize, 10.0f, 10.0f, 10000.0f, "%.0f");
        sliderF("Fade Start", "##GFadeS", &env.gridFadeStart, 1.0f,
                env.gridFadeEnd, "%.0f", "Distance the grid begins to fade");
        sliderF("Fade End", "##GFadeE", &env.gridFadeEnd, env.gridFadeStart,
                10000.0f, "%.0f", "Distance the grid fully fades", true);
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("aabb", "AABB Debug", &env.aabbDebug)) {
        ImGui::BeginDisabled(!env.aabbDebug);
        drawPropertyLabel("Box Color");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::ColorEdit3("##AABBCol", glm::value_ptr(env.debugColor),
            ImGuiColorEditFlags_Float);
        ImGui::EndDisabled();
    }
}

void EnvironmentInspector::drawPipeline(EditorContext& ec) {
    FrameContext& ctx = ec.frame;

    ImGui::TextDisabled("Toggle individual graph passes (advanced).");
    ImGui::Spacing();
    if (ec.renderSystem) {
        auto& graph = ec.renderSystem->getPipeline();
        for (size_t i = 0; i < graph.passCount(); ++i) {
            auto& pass = graph.getPass(i);
            bool enabled = pass.isEnabled();
            if (ImGui::Checkbox(pass.getName().c_str(), &enabled))
                pass.setEnabled(enabled);
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Visibility Culling");
    if (ec.visibilitySystem) {
        auto& settings = ec.visibilitySystem->getSettings();
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
