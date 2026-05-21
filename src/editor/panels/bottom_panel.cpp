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

void BottomPanel::draw(EditorContext& ec) {
    // Tab bar (animation editor + scene statistics). Two sections didn't
    // justify a master-detail layout - tabs are more idiomatic and free
    // up the 150px sidebar for content.
    if (ImGui::BeginTabBar("##BottomTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Animation")) {
            drawAnimationSection(ec);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Statistics")) {
            drawStatisticsSection(ec);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
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
        const float GAP = 8.0f;
        if (iconButton("anplay", anim.playing ? EditorIcon::Pause : EditorIcon::Play,
                       anim.playing, true, anim.playing ? "Pause" : "Play", ih))
            anim.playing = !anim.playing;
        ImGui::SameLine(0, GAP);
        if (iconButton("anstop", EditorIcon::Stop, false, true, "Stop (rewind to start)", ih)) {
            anim.playing = false;
            anim.time = 0.0f;
            previewPose();
        }
        ImGui::SameLine(0, GAP);
        if (iconButton("ankey", EditorIcon::Key, false, true,
                       "Set Key: add/replace keyframes on all 3 tracks at the current time", ih)) {
            anim.positionTrack.setKeyframe(anim.time, tf.position);
            anim.rotationTrack.setKeyframe(anim.time, tf.rotation);
            anim.scaleTrack.setKeyframe(anim.time, tf.scale);
            anim.updateDuration();
        }
        ImGui::SameLine(0, GAP);
        ImGui::Checkbox("Loop", &anim.looping);
        ImGui::SameLine(0, GAP);
        ImGui::SetNextItemWidth(90);
        ImGui::DragFloat("Speed", &anim.speed, 0.005f, 0.0f, 10.0f, "%.2fx");
        ImGui::SameLine(0, GAP);
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
            // Gimbal-lock guard via the shared EulerCache helper.
            m_rotEulerCache.sync(static_cast<int>(k), in);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat3("##v", m_rotEulerCache.degrees(), 0.25f, 0.0f, 0.0f, "%.1f deg")) {
                out = m_rotEulerCache.toQuat();
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

    if (ImGui::BeginTable("##ResCols", 3,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
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

        ImGui::TableNextColumn();
        ImGui::TextDisabled("Animations");
        ImGui::Separator();
        ImGui::Text("Playing: %u  Paused: %u", rc.animPlaying, rc.animPaused);

        ImGui::TableNextColumn();
        ImGui::TextDisabled("Lights");
        ImGui::Separator();
        ImGui::Text("Dir: %u  Point: %u  Spot: %u", rc.lightsDir, rc.lightsPoint, rc.lightsSpot);
        if (rc.lightsDisabled > 0) ImGui::Text("Disabled: %u", rc.lightsDisabled);

        ImGui::EndTable();
    }
}

} // namespace Engine
