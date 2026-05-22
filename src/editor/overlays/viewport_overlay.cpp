#include "overlays/viewport_overlay.h"
#include "framework/editor_common.h"
#include "ui/editor_style.h"

#include "core/math/axes.h"
#include "debug/statistics.h"
#include "platform/threading/thread_pool.h"
#include "system/visibility/visibility.h"
#include "system/camera/camera_controller.h"

namespace Engine {

void ViewportOverlay::updateMetrics(float deltaTime) {
    m_metrics.update(deltaTime);
}

void ViewportOverlay::pushFrameTime(float ms) {
    m_frameTimeHistory[m_frameTimeOffset] = ms;
    m_frameTimeOffset = (m_frameTimeOffset + 1) % FRAME_HISTORY_SIZE;
}

void ViewportOverlay::draw(EditorContext& ec) {
    const FrameContext& ctx = ec.frame;
    const auto& info = ctx.statistics.getFrameInfo();

    ImVec2 regionSize = ImGui::GetContentRegionAvail();
    ImVec2 overlayPos(regionSize.x - 276, 4);

    ImGui::SetCursorPos(overlayPos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    if (ImGui::BeginChild("##StatsOverlay", ImVec2(272, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders)) {

        // Top strip is always-on: large FPS + a few essentials. Collapsibles
        // below let users tune information density without leaving the
        // viewport.
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

        // Frame: entity / draw / pass counts.
        if (ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen)) {
            size_t total = ctx.scene.entityCount();
            size_t vis = ctx.visibility ? ctx.visibility->entries.size() : 0;
            float pct = total > 0 ? (static_cast<float>(vis) / static_cast<float>(total)) * 100.0f : 0.0f;
            ImGui::Text("Entities: %zu  Visible: %zu (%.1f%%)", total, vis, pct);
            ImGui::Text("Draws: %u  Passes: %u", info.renderSystemInfo.drawCalls, info.renderSystemInfo.renderPasses);
            ImGui::Text("Tex binds: %u  Shader sw: %u", info.renderSystemInfo.textureBinds, info.renderSystemInfo.shaderSwitches);
        }

        // System: CPU/RAM/GPU/VRAM. Collapsed by default - useful but noisy.
        if (ImGui::CollapsingHeader("System")) {
            float ramPct = (m_metrics.ramTotalMB() > 0.0f)
                ? (m_metrics.ramUsedMB() / m_metrics.ramTotalMB() * 100.0f) : 0.0f;
            ImGui::Text("CPU  %.0f%% | %.0f%% RAM (%.0f/%.0f GB)",
                         m_metrics.cpuPercent(), ramPct,
                         m_metrics.ramUsedMB() / 1024.0f, m_metrics.ramTotalMB() / 1024.0f);
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

        // Camera: quick access to fovY + exposure on the active camera
        // without leaving the viewport. Edits go through the scene.
        if (ImGui::CollapsingHeader("Camera")) {
            const EntityId camId = ec.cameraController.getCameraEntity().getID();
            if (camId && ctx.scene.has<Camera>(camId)) {
                auto& cam = ctx.scene.get<Camera>(camId);
                if (cam.projection == ProjectionType::Perspective) {
                    float fovDeg = glm::degrees(cam.fovY);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##QFov", &fovDeg, 10.0f, 170.0f, "FOV  %.0f deg")) {
                        cam.fovY = glm::radians(fovDeg);
                        ec.state.markSceneDirty();
                    }
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat("##QExp", &cam.exposure, 0.01f, 0.0f, 10.0f, "Exposure  %.2f")) {
                    ec.state.markSceneDirty();
                }
            } else {
                ImGui::TextDisabled("(no active camera)");
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ViewportOverlay::drawNavigationGizmo(EditorContext& ec) {
    const FrameContext& ctx = ec.frame;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    const ImVec2 regionMax(ec.viewportPos.x + ec.viewportSize.x,
                           ec.viewportPos.y + ec.viewportSize.y);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    constexpr float gizmoSize = 64.0f;
    constexpr float edgeInset = gizmoSize * 0.8f + 12.0f;
    ImVec2 center(regionMax.x - edgeInset, regionMax.y - edgeInset);

    // Transform world axes by camera view rotation
    glm::mat3 viewRot = glm::mat3(ctx.visibility->view);
    glm::vec3 axisX = viewRot * Math::WORLD_AXIS_X_RIGHT;
    glm::vec3 axisY = viewRot * Math::WORLD_AXIS_Y_UP;
    glm::vec3 axisZ = viewRot * Math::WORLD_AXIS_Z_FORWARD;

    constexpr float axisLen = gizmoSize * 0.8f;
    constexpr float labelDotRadius = 8.0f;

    // Six axis endpoints (+X, -X, +Y, -Y, +Z, -Z). Each is clickable for a
    // snap-to-view preset (DCC-standard navigation widget).
    struct Endpoint {
        glm::vec3 worldDir;   // world-space camera direction (negated for label)
        glm::vec3 viewDir;    // view-rotated, for depth sort + screen position
        ImU32     col;
        const char* label;
        bool positive;
    };
    Endpoint endpoints[] = {
        { Math::WORLD_AXIS_X_RIGHT,    axisX, EditorStyle::AXIS_X_U32, "X",  true  },
        { -Math::WORLD_AXIS_X_RIGHT,  -axisX, EditorStyle::AXIS_X_U32, "-X", false },
        { Math::WORLD_AXIS_Y_UP,       axisY, EditorStyle::AXIS_Y_U32, "Y",  true  },
        { -Math::WORLD_AXIS_Y_UP,     -axisY, EditorStyle::AXIS_Y_U32, "-Y", false },
        { Math::WORLD_AXIS_Z_FORWARD,  axisZ, EditorStyle::AXIS_Z_U32, "Z",  true  },
        { -Math::WORLD_AXIS_Z_FORWARD,-axisZ, EditorStyle::AXIS_Z_U32, "-Z", false },
    };

    // Back-to-front by depth in view-space (small z = closer to camera).
    std::sort(std::begin(endpoints), std::end(endpoints),
        [](const Endpoint& a, const Endpoint& b) { return a.viewDir.z < b.viewDir.z; });

    // Background disc.
    drawList->AddCircleFilled(center, gizmoSize * 0.5f, IM_COL32(20, 20, 22, 160), 32);
    drawList->AddCircle(center, gizmoSize * 0.5f, IM_COL32(50, 50, 55, 200), 32, 1.0f);

    // Hit-test the endpoints (front-most wins) using current mouse position.
    // Skip when the mouse isn't over the viewport - otherwise the gizmo
    // would light up while hovering the Inspector / Hierarchy.
    const ImVec2 mp = ImGui::GetMousePos();
    int hoverIdx = -1;
    float hoverDepth = std::numeric_limits<float>::max();
    ImVec2 endPts[6];
    for (int i = 0; i < 6; ++i) {
        endPts[i] = ImVec2(center.x + endpoints[i].viewDir.x * axisLen,
                           center.y - endpoints[i].viewDir.y * axisLen);
        if (!ec.state.viewportHovered) continue;
        const float dx = mp.x - endPts[i].x, dy = mp.y - endPts[i].y;
        if (dx*dx + dy*dy <= labelDotRadius * labelDotRadius
            && endpoints[i].viewDir.z < hoverDepth) {
            hoverDepth = endpoints[i].viewDir.z;
            hoverIdx = i;
        }
    }

    // Click: snap camera. The focus target is the selected entity if any,
    // else the origin. Distance from current camera distance to that target.
    if (hoverIdx >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        glm::vec3 target(0.0f);
        const EditorState& state = ec.state;
        if (state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)
            && ctx.scene.has<Transform>(state.selectedEntity)) {
            target = ctx.scene.get<Transform>(state.selectedEntity).position;
        }
        const float dist = std::max(2.0f, glm::length(ctx.visibility->cameraPosition - target));
        ec.cameraController.viewFrom(ctx.scene, target, endpoints[hoverIdx].worldDir, dist);
    }

    for (int i = 0; i < 6; ++i) {
        const Endpoint& e = endpoints[i];
        const bool isHovered = (i == hoverIdx);
        const bool isPositive = e.positive;

        // Lines only for the positive (front-facing) axes so the gizmo
        // reads cleanly. Negative endpoints are dots only.
        if (isPositive) {
            drawList->AddLine(center, endPts[i], e.col, 2.0f);
        }

        // Endpoint dot: filled for positive, ring for negative; brighter on hover.
        ImU32 dotCol = e.col;
        if (isHovered) dotCol = EditorStyle::HIGHLIGHT_U32;
        if (isPositive)
            drawList->AddCircleFilled(endPts[i], isHovered ? 7.0f : 6.0f, dotCol, 12);
        else
            drawList->AddCircle(endPts[i], isHovered ? 7.0f : 6.0f, dotCol, 12, 1.5f);

        if (isPositive || isHovered) {
            const ImVec2 ts = ImGui::CalcTextSize(e.label);
            ImVec2 labelPos(endPts[i].x - ts.x * 0.5f, endPts[i].y - ts.y * 0.5f);
            drawList->AddText(labelPos, IM_COL32(255, 255, 255, 230), e.label);
        }
    }

    if (hoverIdx >= 0) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("View from %s", endpoints[hoverIdx].label);
    }
}

} // namespace Engine
