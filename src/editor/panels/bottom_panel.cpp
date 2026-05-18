#include "panels/bottom_panel.h"
#include "framework/editor_common.h"
#include "ui/editor_style.h"

#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "system/render/render_graph.h"

#include <cstdio>
#include <string>
#include <filesystem>
#include <system_error>
#include <initializer_list>

namespace Engine {

namespace {
    struct SectionDef { const char* group; const char* name; const char* hint; };

    // Order matches the dispatch switch in BottomPanel::draw().
    const SectionDef kSections[] = {
        {"WORLD", "Rendering",  "Lighting, camera, effects, pipeline"},
        {"TOOLS", "Animation",  "Keyframe editor for the selected entity"},
        {"INFO",  "Statistics", "Component / light / animation counts"},
    };
    constexpr int kSectionCount = static_cast<int>(sizeof(kSections) / sizeof(kSections[0]));

    void sectionHeader(const char* title, const char* hint) {
        drawSectionHeader(title, hint);
    }

    // Collapsible card. When @p enabled is non-null a checkbox precedes the
    // title (the effect's on/off, readable even while collapsed). Returns
    // true when the body should be drawn. Uses the shared card-header look
    // so these read the same as the Inspector's component cards.
    bool cardHeader(const char* id, const char* title, bool* enabled) {
        ImGui::PushID(id);
        if (enabled) { ImGui::Checkbox("##en", enabled); ImGui::SameLine(); }
        bool open = styledCollapsingHeader(title, EditorStyle::ACCENT);
        ImGui::PopID();
        return open;
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

    // Presets only flip effect enables + bloom amount (predictable; they do
    // not clobber paths, colours, grid or exposure tuning).
    void applyPreset(EnvironmentConfig& env, Preset p) {
        switch (p) {
            case Preset::Low:
                env.ssao = false; env.ssr = false; env.taa = false;
                env.dof = false; env.motionBlur = false;
                env.bloomStrength = 0.0f;
                break;
            case Preset::Medium:
                env.ssao = true;  env.ssr = false; env.taa = false;
                env.dof = false; env.motionBlur = false;
                env.bloomStrength = 0.03f;
                break;
            case Preset::High:
                env.ssao = true;  env.ssr = true;  env.taa = false;
                env.dof = false; env.motionBlur = false;
                env.bloomStrength = 0.04f;
                break;
            case Preset::Cinematic:
                env.ssao = true;  env.ssr = true;  env.taa = true;
                env.dof = true;  env.motionBlur = true;
                env.bloomStrength = 0.06f;
                break;
        }
    }

    // Which preset (if any) the current env exactly matches. -1 = Custom
    // (the user hand-tuned a controlled value since applying a preset).
    int detectPreset(const EnvironmentConfig& env) {
        auto matches = [&](bool ssao, bool ssr, bool taa, bool dof,
                           bool mb, float bloom) {
            float d = env.bloomStrength - bloom;
            if (d < 0.0f) d = -d;
            return env.ssao == ssao && env.ssr == ssr && env.taa == taa &&
                   env.dof == dof && env.motionBlur == mb && d < 5e-4f;
        };
        if (matches(false, false, false, false, false, 0.00f)) return 0; // Low
        if (matches(true,  false, false, false, false, 0.03f)) return 1; // Medium
        if (matches(true,  true,  false, false, false, 0.04f)) return 2; // High
        if (matches(true,  true,  true,  true,  true,  0.06f)) return 3; // Cinematic
        return -1;
    }

    const char* presetName(int idx) {
        switch (idx) {
            case 0:  return "Low";
            case 1:  return "Medium";
            case 2:  return "High";
            case 3:  return "Cinematic";
            default: return "Custom";
        }
    }
}

void BottomPanel::draw(EditorContext& ec) {
    ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.11f, 1.0f));
    if (ImGui::BeginChild("##BottomNav", ImVec2(150.0f, avail.y), ImGuiChildFlags_Borders)) {
        const char* lastGroup = nullptr;
        for (int i = 0; i < kSectionCount; ++i) {
            if (kSections[i].group != lastGroup) {
                if (lastGroup) ImGui::Spacing();
                ImGui::TextDisabled("%s", kSections[i].group);
                lastGroup = kSections[i].group;
            }
            ImGui::Indent(8.0f);
            if (ImGui::Selectable(kSections[i].name, m_selectedSection == i))
                m_selectedSection = i;
            ImGui::Unindent(8.0f);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 6);

    if (ImGui::BeginChild("##BottomDetail", ImVec2(0, avail.y), ImGuiChildFlags_Borders)) {
        const auto& s = kSections[m_selectedSection];
        sectionHeader(s.name, s.hint);

        switch (m_selectedSection) {
            case 0: drawRenderingSection(ec);   break;
            case 1: drawAnimationSection(ec);   break;
            case 2: drawStatisticsSection(ec);  break;
            default: break;
        }
    }
    ImGui::EndChild();
}

void BottomPanel::drawRenderingSection(EditorContext& ec) {
    if (!ec.renderSystem) {
        ImGui::TextDisabled("Render system unavailable.");
        return;
    }

    drawPresetBar(ec);
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##RenderTabs")) {
        if (ImGui::BeginTabItem("Lighting")) { ImGui::Spacing(); drawLightingTab(ec); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Camera"))   { ImGui::Spacing(); drawCameraTab(ec);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Effects"))  { ImGui::Spacing(); drawEffectsTab(ec);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Scene"))    { ImGui::Spacing(); drawSceneTab(ec);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Pipeline")) { ImGui::Spacing(); drawPipelineTab(ec); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
}

void BottomPanel::drawPresetBar(EditorContext& ec) {
    auto& env = ec.renderSystem->getEnvironment();

    const int active = detectPreset(env);

    ImGui::TextDisabled("Preset");
    ImGui::SameLine();

    // The active preset's button is tinted with the editor accent so it is
    // obvious which one the renderer is on; "Custom" once a value is tuned.
    auto presetButton = [&](const char* label, Preset p, int idx) {
        const bool on = (active == idx);
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button,        EditorStyle::ACCENT);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  EditorStyle::ACCENT);
        }
        if (ImGui::SmallButton(label)) applyPreset(env, p);
        if (on) ImGui::PopStyleColor(3);
        ImGui::SameLine();
    };
    presetButton("Low",       Preset::Low,       0);
    presetButton("Medium",    Preset::Medium,    1);
    presetButton("High",      Preset::High,      2);
    presetButton("Cinematic", Preset::Cinematic, 3);

    ImGui::SameLine(0, 16);
    if (ImGui::SmallButton("Reset Defaults")) {
        const std::string ep = env.environmentMapPath;
        const std::string lp = env.colorLutPath;
        env = EnvironmentConfig{};
        env.environmentMapPath = ep;   // keep the asset references
        env.colorLutPath       = lp;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Restore documented defaults (keeps the HDR / LUT paths)");

    ImGui::SameLine(0, 16);
    ImGui::TextColored(active >= 0 ? EditorStyle::ACCENT
                                   : ImVec4(0.65f, 0.65f, 0.65f, 1.0f),
        "Active: %s", presetName(active));
}

void BottomPanel::drawLightingTab(EditorContext& ec) {
    auto& env = ec.renderSystem->getEnvironment();

    // IBL on/off is "has a path"; remember the last path so a toggle is
    // lossless.
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
            ImGui::TextDisabled("Active: %s", env.environmentMapPath.c_str());
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (cardHeader("sky", "Analytic Sky (Preetham)", &env.proceduralSky)) {
        ImGui::BeginDisabled(!env.proceduralSky);
        sliderF("Turbidity", "##SkyTurb", &env.skyTurbidity, 1.7f, 10.0f, "%.2f",
                "Haze: ~2 clear, ~10 hazy");
        sliderF("Intensity", "##SkyInt", &env.skyIntensity, 0.0f, 10.0f, "%.2f",
                "Sky brightness multiplier");
        ImGui::TextDisabled("Sun direction = scene directional light");
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

void BottomPanel::drawCameraTab(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    auto& env = ec.renderSystem->getEnvironment();

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

void BottomPanel::drawEffectsTab(EditorContext& ec) {
    auto& env = ec.renderSystem->getEnvironment();

    // Bloom "on" = strength > 0; remember the last strength.
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
            ImGui::TextDisabled("Active: %s", env.colorLutPath.c_str());
        ImGui::EndDisabled();
    }
}

void BottomPanel::drawSceneTab(EditorContext& ec) {
    EditorState& state = ec.state;
    auto& env = ec.renderSystem->getEnvironment();

    ImGui::Checkbox("Wireframe", &state.wireframe);

    ImGui::Spacing();
    if (cardHeader("grid", "Grid", nullptr)) {
        sliderF("Cell Size", "##GScale", &env.gridScale, 0.1f, 100.0f, "%.1f",
                "World units per grid cell");
        drawPropertyLabel("Extent");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##GSize", &env.gridSize, 10.0f, 10.0f, 10000.0f, "%.0f");
        sliderF("Fade Start", "##GFadeS", &env.gridFadeStart, 1.0f,
                env.gridFadeEnd, "%.0f", "Distance the grid begins to fade");
        sliderF("Fade End", "##GFadeE", &env.gridFadeEnd, env.gridFadeStart,
                10000.0f, "%.0f", "Distance the grid fully fades", true);
    }

    ImGui::Spacing();
    if (cardHeader("aabb", "AABB Debug", nullptr)) {
        drawPropertyLabel("Box Color");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::ColorEdit3("##AABBCol", glm::value_ptr(env.debugColor),
            ImGuiColorEditFlags_Float);
    }
}

void BottomPanel::drawPipelineTab(EditorContext& ec) {
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

void BottomPanel::drawAnimationSection(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;
    Scene& scene = ctx.scene;
    EntityId id = state.selectedEntity;

    if (!id || !scene.isAlive(id)) {
        ImGui::TextDisabled("Select an entity (viewport or Hierarchy) to animate it.");
        return;
    }

    char nameBuf[64];
    getEntityDisplayName(scene, id, nameBuf, sizeof(nameBuf));
    ImGui::Text("Target: %s  (#%u)", nameBuf, id.index);

    if (!scene.has<Transform>(id)) {
        ImGui::TextDisabled("Animation drives a Transform - add a Transform component first.");
        return;
    }

    Transform& tf = scene.get<Transform>(id);

    auto editor = [&](Animation& anim) {
        auto previewPose = [&]() {
            if (!anim.positionTrack.isEmpty()) tf.position = anim.positionTrack.getValue(anim.time);
            if (!anim.rotationTrack.isEmpty()) tf.rotation = anim.rotationTrack.getValue(anim.time);
            if (!anim.scaleTrack.isEmpty())    tf.scale    = anim.scaleTrack.getValue(anim.time);
            HierarchyOperations::markDirty(scene, id);
        };

        float ih = ImGui::GetFrameHeight();
        const float kGap = 8.0f;
        if (iconButton("anplay", anim.playing ? EditorIcon::Pause : EditorIcon::Play,
                       anim.playing, true, anim.playing ? "Pause" : "Play", ih))
            anim.playing = !anim.playing;
        ImGui::SameLine(0, kGap);
        if (iconButton("anstop", EditorIcon::Stop, false, true, "Stop (rewind to start)", ih)) {
            anim.playing = false;
            anim.time = 0.0f;
            previewPose();
        }
        ImGui::SameLine(0, kGap);
        if (iconButton("ankey", EditorIcon::Key, false, true,
                       "Set Key: add/replace keyframes on all 3 tracks at the current time", ih)) {
            anim.positionTrack.setKeyframe(anim.time, tf.position);
            anim.rotationTrack.setKeyframe(anim.time, tf.rotation);
            anim.scaleTrack.setKeyframe(anim.time, tf.scale);
            anim.updateDuration();
        }
        ImGui::SameLine(0, kGap);
        ImGui::Checkbox("Loop", &anim.looping);
        ImGui::SameLine(0, kGap);
        ImGui::SetNextItemWidth(90);
        ImGui::DragFloat("Speed", &anim.speed, 0.005f, 0.0f, 10.0f, "%.2fx");
        ImGui::SameLine(0, kGap);
        ImGui::SetNextItemWidth(110);
        float lengthEdit = anim.length;
        if (ImGui::InputFloat("Length", &lengthEdit, 0.1f, 1.0f, "%.2f s")) {
            anim.length = std::max(0.0f, lengthEdit);
            anim.updateDuration();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Animation length in seconds (0 = auto from the last keyframe)");

        anim.updateDuration();
        float dur = anim.duration;

        ImGui::Spacing();
        {
            const float laneH  = 16.0f;
            const float rulerH = 18.0f;
            const float h = rulerH + laneH * 3.0f + 6.0f;
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("##timeline", ImVec2(w, h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(18, 18, 20, 255), 3.0f);

            float D = dur > 1e-4f ? dur : 1.0f;
            auto timeToX = [&](float t) { return p0.x + (t / D) * w; };
            auto xToTime = [&](float x) { return std::clamp(((x - p0.x) / w) * D, 0.0f, D); };

            const int ticks = 10;
            for (int i = 0; i <= ticks; ++i) {
                float t = D * static_cast<float>(i) / ticks;
                float x = timeToX(t);
                dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + rulerH * 0.5f), IM_COL32(90, 90, 95, 255));
                char lab[16];
                snprintf(lab, sizeof(lab), "%.2f", t);
                dl->AddText(ImVec2(x + 2, p0.y + 1), IM_COL32(150, 150, 155, 255), lab);
            }

            struct Lane { ImU32 c; const char* n; const std::vector<float>* times; };
            Lane lanes[3] = {
                {EditorStyle::AXIS_X_U32, "P", &anim.positionTrack.getTimes()},
                {EditorStyle::AXIS_Y_U32, "R", &anim.rotationTrack.getTimes()},
                {EditorStyle::AXIS_Z_U32, "S", &anim.scaleTrack.getTimes()},
            };
            auto laneY = [&](int i) {
                return p0.y + rulerH + laneH * static_cast<float>(i) + laneH * 0.5f;
            };

            // Keyframe dot under the mouse -> hover highlight + grab target.
            ImVec2 mp = ImGui::GetIO().MousePos;
            int   hovTrack = -1;
            float hovTime  = 0.0f;
            for (int i = 0; i < 3; ++i) {
                float ly = laneY(i);
                for (float t : *lanes[i].times) {
                    float dx = timeToX(t) - mp.x, dy = ly - mp.y;
                    if (dx * dx + dy * dy <= 49.0f) { hovTrack = i; hovTime = t; }
                }
            }

            auto moveDot = [&](auto& trk, float fromT, float toT) {
                const auto& ts = trk.getTimes();
                for (size_t k = 0; k < ts.size(); ++k) {
                    if (ts[k] == fromT) { trk.setKeyframeTime(k, toT); return; }
                }
            };

            if (ImGui::IsItemActivated()) {
                if (hovTrack >= 0) { m_animDotTrack = hovTrack; m_animDotTime = hovTime; }
                else m_animDotTrack = -1;
            }

            if (ImGui::IsItemActive()) {
                float mt = xToTime(mp.x);
                if (m_animDotTrack == 0)      moveDot(anim.positionTrack, m_animDotTime, mt);
                else if (m_animDotTrack == 1) moveDot(anim.rotationTrack, m_animDotTime, mt);
                else if (m_animDotTrack == 2) moveDot(anim.scaleTrack,    m_animDotTime, mt);
                if (m_animDotTrack >= 0) { m_animDotTime = mt; anim.updateDuration(); }
                else { anim.time = mt; anim.playing = false; }
                previewPose();
            }
            if (ImGui::IsItemDeactivated()) m_animDotTrack = -1;

            for (int i = 0; i < 3; ++i) {
                float ly = laneY(i);
                dl->AddText(ImVec2(p0.x + 3, ly - 7), lanes[i].c, lanes[i].n);
                dl->AddLine(ImVec2(p0.x + 16, ly), ImVec2(p0.x + w, ly), IM_COL32(45, 45, 50, 255));
                for (float t : *lanes[i].times) {
                    bool hot = (i == hovTrack && t == hovTime)
                            || (i == m_animDotTrack && t == m_animDotTime);
                    dl->AddCircleFilled(ImVec2(timeToX(t), ly), hot ? 5.5f : 3.5f,
                                        hot ? EditorStyle::HIGHLIGHT_U32 : lanes[i].c);
                }
            }

            float px = timeToX(anim.time);
            dl->AddLine(ImVec2(px, p0.y), ImVec2(px, p0.y + h), EditorStyle::HIGHLIGHT_U32, 1.5f);

            if (hovTrack >= 0 && ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        ImGui::SetNextItemWidth(160);
        float prevTime = anim.time;
        if (ImGui::InputFloat("Time", &anim.time, 0.01f, 0.1f, "%.3f s")) {
            anim.time = std::clamp(anim.time, 0.0f, std::max(dur, 0.0f));
            if (anim.time != prevTime) { anim.playing = false; previewPose(); }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("/ %.2f s", dur);

        ImGui::Spacing();
        ImGui::SeparatorText("Tracks");

        auto vec3Editor = [](size_t, const glm::vec3& in, glm::vec3& out) -> bool {
            out = in;
            ImGui::SetNextItemWidth(-1);
            return ImGui::DragFloat3("##v", glm::value_ptr(out), 0.01f, 0.0f, 0.0f, "%.3f");
        };
        auto quatEditor = [this](size_t k, const glm::quat& in, glm::quat& out) -> bool {
            // Gimbal-lock guard (same as InspectorPanel): keep the edited
            // Euler as the source of truth; only re-derive from the stored
            // quaternion when this keyframe's rotation changed outside the
            // drag (different keyframe row, entity switch, keyframe add).
            const int key = static_cast<int>(k);
            const glm::quat cached = glm::quat(glm::radians(m_rotEulerDeg));
            if (m_rotEulerKey != key || glm::abs(glm::dot(cached, in)) < 0.9999f) {
                m_rotEulerDeg = glm::degrees(glm::eulerAngles(in));
                m_rotEulerKey = key;
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat3("##v", glm::value_ptr(m_rotEulerDeg), 0.25f, 0.0f, 0.0f, "%.1f deg")) {
                out = glm::normalize(glm::quat(glm::radians(m_rotEulerDeg)));
                return true;
            }
            out = in;
            return false;
        };

        float ih2 = ImGui::GetFrameHeight();
        auto trackEditor = [&](const char* label, const char* tag, auto& track,
                               auto recordVal, auto valueEditor) {
            char hdr[48];
            snprintf(hdr, sizeof(hdr), "%s  (%zu)###%s", label, track.keyframeCount(), tag);
            if (!ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_DefaultOpen)) return;

            char addId[16];
            snprintf(addId, sizeof(addId), "ka%s", tag);
            if (iconButton(addId, EditorIcon::Plus, false, true,
                           "Add/replace a keyframe at the current time from the live transform", ih2)) {
                track.setKeyframe(anim.time, recordVal());
                anim.updateDuration();
                previewPose();
            }
            ImGui::SameLine();
            char clrId[16];
            snprintf(clrId, sizeof(clrId), "kc%s", tag);
            if (iconButton(clrId, EditorIcon::Trash, false, track.keyframeCount() > 0,
                           "Clear every keyframe on this track", ih2)) {
                track.clear();
                anim.updateDuration();
            }
            ImGui::SameLine(0, 12);
            ImGui::SetNextItemWidth(-1);
            char easeId[24];
            snprintf(easeId, sizeof(easeId), "##e%s", tag);
            EasingFunction f = track.getEasing();
            if (drawEasingCombo(easeId, f)) track.setEasing(f);

            size_t count = track.keyframeCount();
            if (count == 0) {
                ImGui::TreePop();
                return;
            }

            using V = decltype(recordVal());

            char tableId[16];
            snprintf(tableId, sizeof(tableId), "##kt%s", tag);
            if (ImGui::BeginTable(tableId, 4, ImGuiTableFlags_Borders
                    | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("#");
                ImGui::TableSetupColumn("Time");
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##del");
                ImGui::TableHeadersRow();

                int retimeIdx = -1, deleteIdx = -1, valueIdx = -1;
                float retimeVal = 0.0f;
                V newVal{};

                const auto& times  = track.getTimes();
                const auto& values = track.getValues();
                for (size_t k = 0; k < count; ++k) {
                    ImGui::TableNextRow();
                    ImGui::PushID(static_cast<int>(k));

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%zu", k);

                    ImGui::TableNextColumn();
                    float tt = times[k];
                    ImGui::SetNextItemWidth(74);
                    ImGui::InputFloat("##t", &tt, 0.0f, 0.0f, "%.3f");
                    if (ImGui::IsItemDeactivatedAfterEdit() && tt != times[k]) {
                        retimeIdx = static_cast<int>(k);
                        retimeVal = tt;
                    }

                    ImGui::TableNextColumn();
                    V tmp{};
                    if (valueEditor(k, values[k], tmp)) {
                        valueIdx = static_cast<int>(k);
                        newVal = tmp;
                    }

                    ImGui::TableNextColumn();
                    if (iconButton("kdel", EditorIcon::Cross, false, true,
                                   "Delete this keyframe", ih2))
                        deleteIdx = static_cast<int>(k);
                    ImGui::PopID();
                }
                ImGui::EndTable();

                if (deleteIdx >= 0) {
                    track.removeKeyframe(static_cast<size_t>(deleteIdx));
                    anim.updateDuration();
                    previewPose();
                } else if (retimeIdx >= 0) {
                    track.setKeyframeTime(static_cast<size_t>(retimeIdx), std::max(0.0f, retimeVal));
                    anim.updateDuration();
                    previewPose();
                } else if (valueIdx >= 0) {
                    track.setKeyframeValue(static_cast<size_t>(valueIdx), newVal);
                    previewPose();
                }
            }
            ImGui::TreePop();
        };

        trackEditor("Position", "P", anim.positionTrack, [&] { return tf.position; }, vec3Editor);
        trackEditor("Rotation", "R", anim.rotationTrack, [&] { return tf.rotation; }, quatEditor);
        trackEditor("Scale", "S", anim.scaleTrack, [&] { return tf.scale; }, vec3Editor);
    };

    if (!scene.has<Animation>(id)) {
        Animation preview;
        preview.length = 5.0f;

        ImVec2 ovStart = ImGui::GetCursorScreenPos();
        float ovW = ImGui::GetContentRegionAvail().x;

        ImGui::BeginDisabled();
        editor(preview);
        ImGui::EndDisabled();

        float ovEndY = ImGui::GetCursorScreenPos().y;
        float bw = 240.0f;
        float bh = ImGui::GetFrameHeight() + 10.0f;
        ImVec2 bpos(ovStart.x + (ovW - bw) * 0.5f,
                    (ovStart.y + ovEndY) * 0.5f - bh * 0.5f);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(bpos.x - 14, bpos.y - 14), ImVec2(bpos.x + bw + 14, bpos.y + bh + 14),
            IM_COL32(18, 18, 22, 238), 6.0f);
        ImGui::SetCursorScreenPos(bpos);
        if (ImGui::Button("Add Animation Component", ImVec2(bw, bh))) {
            scene.add(Entity{id}, Animation{});
            Animation& na = scene.get<Animation>(id);
            na.length = 5.0f;
            na.updateDuration();
        }
        return;
    }

    editor(scene.get<Animation>(id));
}

void BottomPanel::drawStatisticsSection(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    auto& scene = ctx.scene;

    // Update cached counts periodically (every 0.5s), not every frame
    m_resourceCounts.updateTimer += ctx.deltaTime;
    if (m_resourceCounts.updateTimer >= 0.5f) {
        m_resourceCounts.updateTimer = 0.0f;
        auto& rc = m_resourceCounts;
        rc.transforms  = scene.count<Transform>();
        rc.meshes      = scene.count<Mesh>();
        rc.lights      = scene.count<Light>();
        rc.cameras     = scene.count<Camera>();
        rc.animations  = scene.count<Animation>();
        rc.hierarchies = scene.count<Hierarchy>();
        rc.names       = scene.count<Name>();

        rc.animPlaying = rc.animPaused = 0;
        scene.forEach<Animation>([&](EntityId, const Animation& a) {
            if (a.playing) ++rc.animPlaying; else ++rc.animPaused;
        });

        rc.lightsDir = rc.lightsPoint = rc.lightsSpot = rc.lightsDisabled = 0;
        scene.forEach<Light>([&](EntityId, const Light& l) {
            if (!l.enabled) { ++rc.lightsDisabled; return; }
            switch (l.type) {
                case LightType::Directional: ++rc.lightsDir; break;
                case LightType::Point: ++rc.lightsPoint; break;
                case LightType::Spot: ++rc.lightsSpot; break;
            }
        });
    }

    const auto& rc = m_resourceCounts;
    float colW = ImGui::GetContentRegionAvail().x / 3.0f;

    ImGui::Columns(3, "##ResCols", true);
    ImGui::SetColumnWidth(0, colW);
    ImGui::SetColumnWidth(1, colW);

    ImGui::TextDisabled("Component Counts");
    ImGui::Separator();
    struct CI { const char* n; size_t c; };
    CI comps[] = {
        {"Transform", rc.transforms}, {"Mesh", rc.meshes},
        {"Light", rc.lights}, {"Camera", rc.cameras},
        {"Animation", rc.animations}, {"Hierarchy", rc.hierarchies},
        {"Name", rc.names},
    };
    for (const auto& co : comps) ImGui::Text("%-12s %zu", co.n, co.c);

    ImGui::NextColumn();

    ImGui::TextDisabled("Animations");
    ImGui::Separator();
    ImGui::Text("Playing: %u  Paused: %u", rc.animPlaying, rc.animPaused);

    ImGui::NextColumn();

    ImGui::TextDisabled("Lights");
    ImGui::Separator();
    ImGui::Text("Dir: %u  Point: %u  Spot: %u", rc.lightsDir, rc.lightsPoint, rc.lightsSpot);
    if (rc.lightsDisabled > 0) ImGui::Text("Disabled: %u", rc.lightsDisabled);

    ImGui::Columns(1);
}

} // namespace Engine
