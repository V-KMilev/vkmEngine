#include "panels/bottom_panel.h"

#include <chrono>
#include <cstdio>
#include <ctime>

#include "debug/gpu_timing.h"
#include "debug/shader_error_log.h"
#include "framework/editor_common.h"
#include "system/render/render_graph.h"
#include "system/render/render_graph_resource.h"
#include "system/render/render_pass.h"
#include "system/render/render_system.h"
#include "ui/editor_style.h"

namespace Engine {

void BottomPanel::draw(EditorContext& ec) {
    if (ImGui::BeginTabBar("##BottomTabs")) {
        if (ImGui::BeginTabItem("Animation")) {
            drawAnimationSection(ec);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("GPU")) {
            drawGpuProfilerSection();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Render Graph")) {
            drawRenderGraphSection(ec);
            ImGui::EndTabItem();
        }
        // Append (N) to the tab label when there are pending errors so
        // the operator notices without leaving the Animation tab.
        char shaderLabel[64];
        const std::size_t errCount = ShaderErrorLog::get().size();
        if (errCount > 0) {
            std::snprintf(shaderLabel, sizeof(shaderLabel), "Shader Errors (%zu)###bp_shaders", errCount);
        } else {
            std::snprintf(shaderLabel, sizeof(shaderLabel), "Shader Errors###bp_shaders");
        }
        if (ImGui::BeginTabItem(shaderLabel)) {
            drawShaderErrorsSection();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void BottomPanel::drawRenderGraphSection(EditorContext& ec) {
    const RenderGraph& graph = ec.renderSystem.getGraph();
    const std::size_t n = graph.passCount();

    if (n == 0) {
        ImGui::TextDisabled("Graph empty - no passes registered.");
        return;
    }

    // Per-resource use counter: number of active passes that touch it.
    std::size_t resourcesUsed = 0;
    for (std::uint32_t r = 0; r < RG_RESOURCE_COUNT; ++r) {
        if (graph.lifetime(static_cast<RGResource>(r)).used()) ++resourcesUsed;
    }

    ImGui::Text("%zu passes, %zu transient resources in use",
                n, resourcesUsed);
    ImGui::TextDisabled("R = read, W = write, RW = both. Faint = within [firstWrite, lastRead].");
    ImGui::Separator();

    constexpr ImU32 kCellWrite = IM_COL32(200,  60,  60, 100);  // red-ish
    constexpr ImU32 kCellRead  = IM_COL32( 70, 140, 220, 100);  // cyan-ish
    constexpr ImU32 kCellBoth  = IM_COL32(180,  80, 200, 130);  // magenta-ish
    constexpr ImU32 kCellSpan  = IM_COL32(120, 120, 120,  35);  // dim in-lifetime
    constexpr int   kPassColPx = 38;

    const int totalCols = 2 + static_cast<int>(n);

    if (ImGui::BeginTable("##rg_matrix", totalCols,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupScrollFreeze(2, 1);
        ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Range",    ImGuiTableColumnFlags_WidthFixed, 70.0f);
        for (std::size_t i = 0; i < n; ++i) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%zu", i);
            ImGui::TableSetupColumn(buf, ImGuiTableColumnFlags_WidthFixed,
                                    static_cast<float>(kPassColPx));
        }
        ImGui::TableHeadersRow();

        // Hover-tooltip the pass-index column headers with their names so the
        // narrow columns stay readable. ImGui doesn't expose a per-header
        // tooltip hook directly so we walk the header row a second time.
        for (std::size_t i = 0; i < n; ++i) {
            ImGui::TableSetColumnIndex(2 + static_cast<int>(i));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Pass %zu\n%s",
                    i, graph.getPass(i).getName().c_str());
            }
        }

        for (std::uint32_t r = 0; r < RG_RESOURCE_COUNT; ++r) {
            const auto rid = static_cast<RGResource>(r);
            const auto& lt = graph.lifetime(rid);
            const bool used = lt.used();

            // Hide rows that nothing references this frame to keep the matrix tight.
            if (!used) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(rgResourceName(rid));

            ImGui::TableSetColumnIndex(1);
            if (lt.firstWrite >= 0 && lt.lastRead >= 0) {
                ImGui::Text("[%d..%d]", lt.firstWrite, lt.lastRead);
            } else if (lt.firstWrite >= 0) {
                ImGui::Text("W@%d",  lt.firstWrite);
            } else {
                ImGui::Text("R@%d",  lt.lastRead);
            }

            for (std::size_t i = 0; i < n; ++i) {
                ImGui::TableSetColumnIndex(2 + static_cast<int>(i));

                bool reads = false, writes = false;
                for (RGResource rd : graph.passReads(i))  if (rd == rid) { reads  = true; break; }
                for (RGResource wr : graph.passWrites(i)) if (wr == rid) { writes = true; break; }

                ImU32 bg = 0;
                const char* label = "";
                if (reads && writes) { bg = kCellBoth;  label = "RW"; }
                else if (writes)     { bg = kCellWrite; label = "W";  }
                else if (reads)      { bg = kCellRead;  label = "R";  }
                else {
                    const int pi = static_cast<int>(i);
                    if (lt.firstWrite >= 0 && lt.lastRead >= 0
                        && pi >= lt.firstWrite && pi <= lt.lastRead) {
                        bg = kCellSpan;
                    }
                }
                if (bg) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, bg);
                if (*label) ImGui::TextUnformatted(label);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Pass declarations")) {
        for (std::size_t i = 0; i < n; ++i) {
            ImGui::Text("[%zu] %s", i, graph.getPass(i).getName().c_str());
            const auto& reads  = graph.passReads(i);
            const auto& writes = graph.passWrites(i);
            if (!writes.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("  writes:");
                for (std::size_t k = 0; k < writes.size(); ++k) {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(rgResourceName(writes[k]));
                }
            }
            if (!reads.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("  reads:");
                for (std::size_t k = 0; k < reads.size(); ++k) {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(rgResourceName(reads[k]));
                }
            }
        }
    }
}

void BottomPanel::drawGpuProfilerSection() {
    const auto passes = GpuTimingPool::get().snapshot();
    if (passes.empty()) {
        ImGui::TextDisabled("No passes registered yet - the first frame will populate this.");
        return;
    }

    double totalLast = 0.0;
    double totalAvg  = 0.0;
    for (const auto& p : passes) { totalLast += p.last; totalAvg += p.avg; }

    ImGui::Text("Total: %.3f ms last, %.3f ms avg (%zu passes)",
                totalLast, totalAvg, passes.size());
    ImGui::TextDisabled("Per-pass GL_TIME_ELAPSED, double-buffered (1-frame lag).");
    ImGui::Separator();

    if (ImGui::BeginTable("##gpu_passes", 5,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Pass",   ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Last ms", ImGuiTableColumnFlags_WidthFixed,  70.0f);
        ImGui::TableSetupColumn("Avg ms",  ImGuiTableColumnFlags_WidthFixed,  70.0f);
        ImGui::TableSetupColumn("p99 ms",  ImGuiTableColumnFlags_WidthFixed,  70.0f);
        ImGui::TableSetupColumn("History", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < passes.size(); ++i) {
            const auto& p = passes[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(p.name.empty() ? "?" : p.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", p.last);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", p.avg);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", p.p99);
            ImGui::TableSetColumnIndex(4);
            // PlotLines wants a contiguous float array - PassStats keeps the
            // ring un-rotated, which is fine: the visible squiggle wraps but
            // the relative shape is still readable, and rotating each frame
            // would be wasted work.
            const float scaleMin = 0.0f;
            const float scaleMax = static_cast<float>(p.maxV > 0.0 ? p.maxV : 1.0);
            char overlay[16];
            std::snprintf(overlay, sizeof(overlay), "%.2f ms", p.maxV);
            ImGui::PushID(static_cast<int>(i));
            ImGui::PlotLines("##plot", p.ring.data(),
                static_cast<int>(p.ring.size()),
                /*offset=*/static_cast<int>(p.cursor),
                overlay, scaleMin, scaleMax,
                ImVec2(-1.0f, ImGui::GetTextLineHeight() * 1.5f));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void BottomPanel::drawShaderErrorsSection() {
    auto entries = ShaderErrorLog::get().snapshot();
    ImGui::Text("%zu entr%s (newest first, cap %zu)",
                entries.size(), entries.size() == 1 ? "y" : "ies",
                ShaderErrorLog::kCapacity);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) ShaderErrorLog::get().clearAll();
    ImGui::Separator();

    if (entries.empty()) {
        ImGui::TextDisabled("No shader errors. Hot-reload that fails to compile will land here.");
        return;
    }

    if (ImGui::BeginChild("##shader_err_list", ImVec2(0, 0), false,
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

            char header[160];
            if (e.repeatCount > 1) {
                std::snprintf(header, sizeof(header), "[%s] %s  x%u",
                              ts, e.shaderName.c_str(), e.repeatCount);
            } else {
                std::snprintf(header, sizeof(header), "[%s] %s",
                              ts, e.shaderName.c_str());
            }
            ImGui::PushID(&e);
            if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
                if (!e.definesSummary.empty()) {
                    ImGui::TextDisabled("defines: %s", e.definesSummary.c_str());
                }
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
            state.markSceneDirty();
        }
        ImGui::SameLine(0, GAP);
        if (ImGui::Checkbox("Loop", &anim.looping)) state.markSceneDirty();
        ImGui::SameLine(0, GAP);
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("Speed", &anim.speed, 0.005f, 0.0f, 10.0f, "%.2fx"))
            state.markSceneDirty();
        ImGui::SameLine(0, GAP);
        ImGui::SetNextItemWidth(110);
        float lengthEdit = anim.length;
        if (ImGui::InputFloat("Length", &lengthEdit, 0.1f, 1.0f, "%.2f s")) {
            anim.length = std::max(0.0f, lengthEdit);
            anim.updateDuration();
            state.markSceneDirty();
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
                if (m_animDotTrack >= 0) { anim.updateDuration(); state.markSceneDirty(); }
                else { anim.time = mt; anim.playing = false; }
                previewPose();
            }
            if (ImGui::IsItemDeactivated()) m_animDotTrack = -1;

            for (int i = 0; i < 3; ++i) {
                float ly = laneY(i);
                dl->AddText(ImVec2(p0.x + 3, ly - 7), lanes[i].c, lanes[i].n);
                dl->AddLine(ImVec2(p0.x + 16, ly), ImVec2(p0.x + w, ly), IM_COL32(45, 45, 50, 255));
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
                state.markSceneDirty();
            }
            ImGui::SameLine();
            char clrId[16];
            snprintf(clrId, sizeof(clrId), "kc%s", tag);
            if (iconButton(clrId, EditorIcon::Trash, false, track.keyframeCount() > 0,
                           "Clear every keyframe on this track", ih2)) {
                track.clear();
                anim.updateDuration();
                state.markSceneDirty();
            }
            ImGui::SameLine(0, 12);
            ImGui::SetNextItemWidth(-1);
            char easeId[24];
            snprintf(easeId, sizeof(easeId), "##e%s", tag);
            EasingFunction f = track.getEasing();
            if (drawEasingCombo(easeId, f)) { track.setEasing(f); state.markSceneDirty(); }

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
                    state.markSceneDirty();
                } else if (retimeIdx >= 0) {
                    track.setKeyframeTime(static_cast<size_t>(retimeIdx), std::max(0.0f, retimeVal));
                    anim.updateDuration();
                    previewPose();
                    state.markSceneDirty();
                } else if (valueIdx >= 0) {
                    track.setKeyframeValue(static_cast<size_t>(valueIdx), newVal);
                    previewPose();
                    state.markSceneDirty();
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
            state.markSceneDirty();
        }
        return;
    }

    editor(scene.get<Animation>(id));
}

} // namespace Engine
