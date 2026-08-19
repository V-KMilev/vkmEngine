#include "panels/bottom_panel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>

#include "debug/engine_error_log.h"
#include "framework/component_edit.h"
#include "framework/editor_commands.h"
#include "framework/editor_common.h"
#include "framework/prefab_overrides.h"
#include "ui/editor_style.h"

namespace Vkm::Engine {

void BottomPanel::draw(EditorContext& ec) {
    if (ImGui::BeginTabBar("##BottomTabs")) {
        if (ImGui::BeginTabItem("Animation")) {
            drawAnimationSection(ec);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Errors")) {
            drawErrorsSection(ec.errorLog);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void BottomPanel::drawErrorsSection(EngineErrorLog& errorLog) {
    auto entries = errorLog.snapshot();
    ImGui::Text("%zu entr%s (newest first, cap %zu)",
                entries.size(), entries.size() == 1 ? "y" : "ies",
                EngineErrorLog::CAPACITY);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) errorLog.clearAll();
    ImGui::Separator();

    if (entries.empty()) {
        ImGui::TextDisabled("No errors. Recoverable engine failures (e.g. a script hook that throws) are recorded here.");
        return;
    }

    if (ImGui::BeginChild("##engine_err_list",
                          ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 10), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto& e : entries) {
            const auto tt = std::chrono::system_clock::to_time_t(e.timestamp);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            char ts[16];
            std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);

            char header[192];
            if (e.repeatCount > 1) {
                std::snprintf(header, sizeof(header), "[%s] [%s] %s  x%u",
                              ts, e.category.c_str(), e.source.c_str(), e.repeatCount);
            } else {
                std::snprintf(header, sizeof(header), "[%s] [%s] %s",
                              ts, e.category.c_str(), e.source.c_str());
            }
            ImGui::PushID(&e);
            if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("%s", e.message.c_str());
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
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
        // Undo snapshot. Authoring edits (keys, length, loop, speed) push one
        // coalescing command at the end; play/pause/stop/scrub stay
        // non-undoable (same policy as the Inspector's Animation card).
        const Animation before = anim;
        bool changed = false;

        auto previewPose = [&]() {
            if (!anim.positionTrack.isEmpty()) tf.position = anim.positionTrack.getValue(anim.time);
            if (!anim.rotationTrack.isEmpty()) tf.rotation = anim.rotationTrack.getValue(anim.time);
            if (!anim.scaleTrack.isEmpty())    tf.scale    = anim.scaleTrack.getValue(anim.time);
        };

        float ih = ImGui::GetFrameHeight();
        const float GAP = EditorStyle::px(8.0f);
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
            changed = true;
        }
        ImGui::SameLine(0, GAP);
        if (iconButton("anloop", EditorIcon::Loop, anim.looping, true,
                       anim.looping ? "Looping" : "Play once", ih)) {
            anim.looping = !anim.looping;
            changed = true;
        }
        ImGui::SameLine(0, GAP);
        // Value-embedded prefix, matching the Inspector's hidden-label rows.
        ImGui::SetNextItemWidth(EditorStyle::px(110.0f));
        changed |= ImGui::DragFloat("##animSpeed", &anim.speed, 0.005f, 0.0f, 10.0f, "Speed %.2fx");
        ImGui::SameLine(0, GAP);
        ImGui::SetNextItemWidth(EditorStyle::px(110.0f));
        float lengthEdit = anim.length;
        if (ImGui::InputFloat("Length", &lengthEdit, 0.1f, 1.0f, "%.2f s")) {
            anim.length = std::max(0.0f, lengthEdit);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Animation length in seconds (0 = auto from the last keyframe)");

        const float dur = Animation::computeDuration(anim);

        ImGui::Spacing();
        {
            const float laneH  = 16.0f;
            const float rulerH = EditorStyle::px(18.0f);
            const float h = rulerH + laneH * 3.0f + 6.0f;
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("##timeline", ImVec2(w, h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), EditorStyle::TIMELINE_BG_U32, 3.0f);

            float D = dur > 1e-4f ? dur : 1.0f;
            auto timeToX = [&](float t) { return p0.x + (t / D) * w; };
            auto xToTime = [&](float x) { return std::clamp(((x - p0.x) / w) * D, 0.0f, D); };

            const int ticks = 10;
            for (int i = 0; i <= ticks; ++i) {
                float t = D * static_cast<float>(i) / ticks;
                float x = timeToX(t);
                dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + rulerH * 0.5f), EditorStyle::TIMELINE_TICK_U32);
                char lab[16];
                snprintf(lab, sizeof(lab), "%.2f", t);
                dl->AddText(ImVec2(x + 2, p0.y + 1), EditorStyle::TIMELINE_LABEL_U32, lab);
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
            // Tracked by INDEX rather than time; matching by float equality
            // breaks the moment setKeyframeTime is invoked mid-drag.
            ImVec2 mp = ImGui::GetIO().MousePos;
            int    hovTrack = -1;
            size_t hovIdx   = 0;
            for (int i = 0; i < 3; ++i) {
                float ly = laneY(i);
                const auto& times = *lanes[i].times;
                for (size_t k = 0; k < times.size(); ++k) {
                    float dx = timeToX(times[k]) - mp.x, dy = ly - mp.y;
                    if (dx * dx + dy * dy <= 49.0f) { hovTrack = i; hovIdx = k; }
                }
            }

            auto moveDot = [&](auto& trk, size_t idx, float toT) {
                if (idx < trk.keyframeCount()) trk.setKeyframeTime(idx, toT);
            };

            if (ImGui::IsItemActivated()) {
                if (hovTrack >= 0) { m_animDotTrack = hovTrack; m_animDotIdx = hovIdx; }
                else m_animDotTrack = -1;
            }

            if (ImGui::IsItemActive()) {
                float mt = xToTime(mp.x);
                if (m_animDotTrack == 0)      moveDot(anim.positionTrack, m_animDotIdx, mt);
                else if (m_animDotTrack == 1) moveDot(anim.rotationTrack, m_animDotIdx, mt);
                else if (m_animDotTrack == 2) moveDot(anim.scaleTrack,    m_animDotIdx, mt);
                if (m_animDotTrack >= 0) changed = true;
                else { anim.time = mt; anim.playing = false; }
                previewPose();
            }
            if (ImGui::IsItemDeactivated()) m_animDotTrack = -1;

            for (int i = 0; i < 3; ++i) {
                float ly = laneY(i);
                dl->AddText(ImVec2(p0.x + 3, ly - 7), lanes[i].c, lanes[i].n);
                dl->AddLine(ImVec2(p0.x + 16, ly), ImVec2(p0.x + w, ly), EditorStyle::TIMELINE_LANE_U32);
                const auto& times = *lanes[i].times;
                for (size_t k = 0; k < times.size(); ++k) {
                    bool hot = (i == hovTrack && k == hovIdx)
                            || (i == m_animDotTrack && k == m_animDotIdx);
                    dl->AddCircleFilled(ImVec2(timeToX(times[k]), ly), hot ? 5.5f : 3.5f,
                                        hot ? EditorStyle::HIGHLIGHT_U32 : lanes[i].c);
                }
            }

            float px = timeToX(anim.time);
            dl->AddLine(ImVec2(px, p0.y), ImVec2(px, p0.y + h), EditorStyle::HIGHLIGHT_U32, 1.5f);

            if (hovTrack >= 0 && ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        ImGui::SetNextItemWidth(EditorStyle::px(140.0f));
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
                previewPose();
                changed = true;
            }
            ImGui::SameLine();
            char clrId[16];
            snprintf(clrId, sizeof(clrId), "kc%s", tag);
            if (iconButton(clrId, EditorIcon::Trash, false, track.keyframeCount() > 0,
                           "Clear every keyframe on this track", ih2)) {
                track.clear();
                changed = true;
            }
            ImGui::SameLine(0, 12);
            ImGui::SetNextItemWidth(-1);
            char easeId[24];
            snprintf(easeId, sizeof(easeId), "##e%s", tag);
            EasingFunction f = track.getEasing();
            if (drawEasingCombo(easeId, f)) { track.setEasing(f); changed = true; }

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
                    previewPose();
                    changed = true;
                } else if (retimeIdx >= 0) {
                    track.setKeyframeTime(static_cast<size_t>(retimeIdx), std::max(0.0f, retimeVal));
                    previewPose();
                    changed = true;
                } else if (valueIdx >= 0) {
                    track.setKeyframeValue(static_cast<size_t>(valueIdx), newVal);
                    previewPose();
                    changed = true;
                }
            }
            ImGui::TreePop();
        };

        trackEditor("Position", "P", anim.positionTrack, [&] { return tf.position; }, vec3Editor);
        trackEditor("Rotation", "R", anim.rotationTrack, [&] { return tf.rotation; }, quatEditor);
        trackEditor("Scale", "S", anim.scaleTrack, [&] { return tf.scale; }, vec3Editor);

        if (changed) {
            // tryMerge collapses per-frame drag edits (timeline dots, speed)
            // into one undo step.
            pushEdit<Animation>(scene, ctx.resources, state, id, before, anim, "Edit Animation");
        }
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
        float bw = EditorStyle::px(240.0f);
        float bh = ImGui::GetFrameHeight() + 10.0f;
        ImVec2 bpos(ovStart.x + (ovW - bw) * 0.5f,
                    (ovStart.y + ovEndY) * 0.5f - bh * 0.5f);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(bpos.x - 14, bpos.y - 14), ImVec2(bpos.x + bw + 14, bpos.y + bh + 14),
            EditorStyle::TIMELINE_GHOST_U32, 6.0f);
        ImGui::SetCursorScreenPos(bpos);
        if (ImGui::Button("Add Animation Component", ImVec2(bw, bh))) {
            scene.add(id, Animation{});
            Animation& na = scene.get<Animation>(id);
            na.length = 5.0f;
            // Same undo path as the Inspector's Add Component menu.
            state.commands.push(std::make_unique<AddComponentCommand<Animation>>(
                id, na, "Add Animation"));
            state.markSceneDirty();
            PrefabOverrides::warnComponentIsPrefabs(scene, state, id, "Animation",
                                                    "is not stored in the scene");
        }
        return;
    }

    editor(scene.get<Animation>(id));
}

} // namespace Vkm::Engine
