#include "viewport_overlay.h"
#include "../editor_common.h"
#include "../editor_style.h"

#include <GL/glew.h>

#include "core/math/axes.h"
#include "debug/statistics.h"
#include "platform/threading/thread_pool.h"

namespace Engine {

void ViewportOverlay::updateMetrics(float deltaTime) {
    m_metrics.update(deltaTime);
}

void ViewportOverlay::draw(const FrameContext& ctx, const EditorState& state) {
    ImVec2 regionSize = ImGui::GetContentRegionAvail();
    ImVec2 overlayPos(regionSize.x - 276, 4);

    ImGui::SetCursorPos(overlayPos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    if (ImGui::BeginChild("##StatsOverlay", ImVec2(272, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders)) {
        const auto& info = ctx.statistics.getFrameInfo();

        float avgMs = 0.0f, maxMs = 0.0f, minMs = 1000.0f;
        for (int i = 0; i < FRAME_HISTORY_SIZE; ++i) {
            avgMs += m_frameTimeHistory[i];
            if (m_frameTimeHistory[i] > maxMs) maxMs = m_frameTimeHistory[i];
            if (m_frameTimeHistory[i] > 0.0f && m_frameTimeHistory[i] < minMs)
                minMs = m_frameTimeHistory[i];
        }
        avgMs /= FRAME_HISTORY_SIZE;
        m_frameTimeMax = m_frameTimeMax * 0.95f + maxMs * 0.05f;

        char overlay[64];
        snprintf(overlay, sizeof(overlay), "%.1f FPS | %.2f ms", info.frameRateInfo.frameRate, avgMs);
        ImGui::PlotLines("##FT", m_frameTimeHistory, FRAME_HISTORY_SIZE,
                         m_frameTimeOffset, overlay, 0.0f,
                         std::max(m_frameTimeMax * 1.2f, 1.0f), ImVec2(256, 36));

        ImGui::TextDisabled("Min %.2f  Avg %.2f  Max %.2f", minMs, avgMs, maxMs);

        ImGui::Spacing();

        size_t total = ctx.scene.entityCount();
        size_t vis = ctx.visibility ? ctx.visibility->entries.size() : 0;
        float pct = total > 0 ? (static_cast<float>(vis) / static_cast<float>(total)) * 100.0f : 0.0f;
        ImGui::Text("Entities: %zu  Visible: %zu (%.1f%%)", total, vis, pct);
        ImGui::Text("Draws: %u  Passes: %u", info.renderSystemInfo.drawCalls, info.renderSystemInfo.renderPasses);
        ImGui::Text("Tex binds: %u  Shader sw: %u", info.renderSystemInfo.textureBinds, info.renderSystemInfo.shaderSwitches);

        ImGui::Spacing();
        ImGui::TextDisabled("System");
        ImGui::Separator();
        {
            float ramPct = (m_metrics.ramTotalMB() > 0.0f)
                ? (m_metrics.ramUsedMB() / m_metrics.ramTotalMB() * 100.0f) : 0.0f;
            ImGui::Text("CPU  %.0f%% | %.0f%% RAM (%.0f/%.0f GB)",
                         m_metrics.cpuPercent(), ramPct,
                         m_metrics.ramUsedMB() / 1024.0f, m_metrics.ramTotalMB() / 1024.0f);
        }
        if (m_metrics.hasGpuUtil() || m_metrics.hasVram()) {
            float vramPct = m_metrics.hasVram()
                ? (m_metrics.vramUsedMB() / m_metrics.vramTotalMB() * 100.0f) : 0.0f;
            ImGui::Text("GPU  %.0f%% | %.0f%% VRAM (%.0f/%.0f MB)",
                         m_metrics.gpuPercent(), vramPct,
                         m_metrics.vramUsedMB(), m_metrics.vramTotalMB());
        } else {
            ImGui::TextDisabled("GPU  N/A");
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // The active tool / space / snap state is shown by the viewport toolbar
    // (bottom-left), so no separate text indicator is drawn here.
    (void)state;
}

void ViewportOverlay::drawNavigationGizmo(const FrameContext& ctx, ImVec2 regionMin, ImVec2 regionMax) {
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    float gizmoSize = 60.0f;
    float padding = 16.0f;
    ImVec2 center(regionMax.x - gizmoSize - padding, regionMax.y - gizmoSize - padding);

    // Transform world axes by camera view rotation
    glm::mat3 viewRot = glm::mat3(ctx.visibility->view);
    glm::vec3 axisX = viewRot * Math::WORLD_AXIS_X_RIGHT;
    glm::vec3 axisY = viewRot * Math::WORLD_AXIS_Y_UP;
    glm::vec3 axisZ = viewRot * Math::WORLD_AXIS_Z_FORWARD;

    float axisLen = gizmoSize * 0.8f;

    struct AxisDraw { glm::vec3 dir; ImU32 col; const char* label; };
    AxisDraw axes[] = {
        { axisX, EditorStyle::AXIS_X_U32, "X" },
        { axisY, EditorStyle::AXIS_Y_U32, "Y" },
        { axisZ, EditorStyle::AXIS_Z_U32, "Z" },
    };

    // Sort by depth (draw back-to-front)
    std::sort(std::begin(axes), std::end(axes),
        [](const AxisDraw& a, const AxisDraw& b) { return a.dir.z < b.dir.z; });

    // Background circle
    drawList->AddCircleFilled(center, gizmoSize * 0.5f, IM_COL32(20, 20, 22, 160), 32);
    drawList->AddCircle(center, gizmoSize * 0.5f, IM_COL32(50, 50, 55, 200), 32, 1.0f);

    for (const auto& axis : axes) {
        ImVec2 endPt(center.x + axis.dir.x * axisLen,
                     center.y - axis.dir.y * axisLen);  // Y flipped for screen coords

        drawList->AddLine(center, endPt, axis.col, 2.0f);

        // Arrow tip circle
        drawList->AddCircleFilled(endPt, 5.0f, axis.col, 8);

        // Label
        ImVec2 labelPos(endPt.x - 3.0f, endPt.y - 6.0f);
        drawList->AddText(labelPos, IM_COL32(255, 255, 255, 220), axis.label);
    }
}

} // namespace Engine
