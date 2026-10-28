#include "overlays/viewport_overlay.h"

#include <algorithm>
#include <cstdio>
#include <limits>

#include <imgui.h>
#include <glm/glm.hpp>

#include "core/system.h"
#include "ecs/component/core/transform.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "ui/editor_style.h"
#include "core/math/axes.h"
#include "ecs/component/core/world_transform.h"
#include "system/visibility/visibility.h"
#include "input/camera_controller_system.h"
#include "input/view_framing.h"

namespace Vkm::Engine {

namespace {

// Shared by drawing the axes and reserving their room.
float discSize()       { return EditorStyle::px(64.0f); }
float axisLength()     { return discSize() * 0.8f; }
float endpointRadius() { return EditorStyle::px(8.0f); }
float edgeInset()      { return axisLength() + EditorStyle::px(12.0f); }

} // namespace

float ViewportOverlay::reach() {
    return edgeInset() + axisLength() + endpointRadius();
}

void ViewportOverlay::drawNoCameraNotice(EditorContext& ec) {
    const FrameContext& ctx = ec.frame;
    if (ctx.visibility && ctx.visibility->hasCamera) return;

    // Only a session showing the game gets here; the editor's view always has a camera.
    const char* headline = "No active camera - the game has nothing to render from";
    char detail[160];
    snprintf(
        detail,
        sizeof(detail),
        "Entity > Create > Camera, or tick Active on a Camera card. %s ejects to the editor's view.",
        keyLabel(ec.state.prefs.keybinds.ejectView).buf
    );

    const ImVec2 headSize = ImGui::CalcTextSize(headline);
    const ImVec2 detailSize = ImGui::CalcTextSize(detail);
    const float  lineGap  = ImGui::GetTextLineHeightWithSpacing() - ImGui::GetTextLineHeight();
    const float  blockH   = headSize.y + lineGap + detailSize.y;
    const ImVec2 center(
        ec.viewportPos.x + ec.viewportSize.x * 0.5f,
        ec.viewportPos.y + ec.viewportSize.y * 0.5f
    );

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 blockMin(center.x - std::max(headSize.x, detailSize.x) * 0.5f, center.y - blockH * 0.5f);
    const float padX = EditorStyle::px(14.0f);
    const float padY = EditorStyle::px(12.0f);
    const ImVec2 panelMin(blockMin.x - padX, blockMin.y - padY);
    const ImVec2 panelMax(blockMin.x + std::max(headSize.x, detailSize.x) + padX, blockMin.y + blockH + padY);
    dl->AddRectFilled(panelMin, panelMax, ImGui::GetColorU32(EditorStyle::OVERLAY_BG), EditorStyle::px(6.0f));
    dl->AddText(
        ImVec2(center.x - headSize.x * 0.5f, blockMin.y),
        ImGui::GetColorU32(EditorStyle::WARNING),
        headline
    );
    dl->AddText(
        ImVec2(center.x - detailSize.x * 0.5f, blockMin.y + headSize.y + lineGap),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        detail
    );
}

void ViewportOverlay::drawNavigationGizmo(EditorContext& ec, bool shown) {
    const FrameContext& ctx = ec.frame;
    m_hovered = false;
    if (!shown || !ctx.visibility || !ctx.visibility->hasCamera) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float gizmoSize = discSize();
    const float inset     = edgeInset();
    const ImVec2 center(
        ec.viewportPos.x + ec.viewportSize.x - inset,
        ec.viewportPos.y + ec.viewportSize.y - inset
    );

    glm::mat3 viewRot = glm::mat3(ctx.visibility->camera.view);
    glm::vec3 axisX = viewRot * Math::WORLD_AXIS_X;
    glm::vec3 axisY = viewRot * Math::WORLD_AXIS_Y;
    glm::vec3 axisZ = viewRot * Math::WORLD_AXIS_Z;

    const float axisLen = axisLength();
    const float labelDotRadius = endpointRadius();

    struct Endpoint {
        glm::vec3 worldDir;   // world-space "view from" direction passed to viewFrom
        glm::vec3 viewDir;    // view-rotated, for depth sort + screen position
        ImU32     col;
        const char* label;
        bool positive;
    };
    Endpoint endpoints[] = {
        {  Math::WORLD_AXIS_X,  axisX, EditorStyle::AXIS_X_U32, "X",  true  },
        { -Math::WORLD_AXIS_X, -axisX, EditorStyle::AXIS_X_U32, "-X", false },
        {  Math::WORLD_AXIS_Y,  axisY, EditorStyle::AXIS_Y_U32, "Y",  true  },
        { -Math::WORLD_AXIS_Y, -axisY, EditorStyle::AXIS_Y_U32, "-Y", false },
        {  Math::WORLD_AXIS_Z,  axisZ, EditorStyle::AXIS_Z_U32, "Z",  true  },
        { -Math::WORLD_AXIS_Z, -axisZ, EditorStyle::AXIS_Z_U32, "-Z", false },
    };

    // Back-to-front: the view looks down -Z, so the smallest z is drawn first.
    std::sort(
        std::begin(endpoints),
        std::end(endpoints),
        [](const Endpoint& a, const Endpoint& b) { return a.viewDir.z < b.viewDir.z; }
    );

    // Front-most endpoint (largest view z) wins. Skipped when the pointer is not the
    // viewport's, or the gizmo would light up while hovering the Inspector.
    const ImVec2 mp = ImGui::GetMousePos();
    int hoverIdx = -1;
    float hoverDepth = std::numeric_limits<float>::lowest();
    ImVec2 endPts[6];
    for (int i = 0; i < 6; ++i) {
        endPts[i] = ImVec2(
            center.x + endpoints[i].viewDir.x * axisLen,
            center.y - endpoints[i].viewDir.y * axisLen
        );
        if (!ec.input.navigationMayHover()) continue;
        const float dx = mp.x - endPts[i].x, dy = mp.y - endPts[i].y;
        if (dx*dx + dy*dy <= labelDotRadius * labelDotRadius
            && endpoints[i].viewDir.z > hoverDepth) {
            hoverDepth = endpoints[i].viewDir.z;
            hoverIdx = i;
        }
    }

    // The disc shows only while the pointer is near; at rest it would clutter the scene.
    const float hoverRadius = axisLen + labelDotRadius;
    const float nx = mp.x - center.x, ny = mp.y - center.y;
    if (ec.input.navigationMayHover() && nx * nx + ny * ny <= hoverRadius * hoverRadius) {
        drawList->AddCircleFilled(center, gizmoSize * 0.5f, EditorStyle::NAV_DISC_U32, 32);
        drawList->AddCircle(center, gizmoSize * 0.5f, EditorStyle::NAV_RING_U32, 32, EditorStyle::px(1.0f));
    }

    if (hoverIdx >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        glm::vec3 target(0.0f);
        const EditorState& state = ec.state;
        // World pose: a parented entity's local position would orbit an empty point.
        if (const Transform* at = ctx.scene.tryGet<Transform>(state.selectedEntity)) {
            target = resolvedWorldPosition(ctx.scene, state.selectedEntity, *at);
        }
        const float dist = std::max(2.0f, glm::length(ctx.visibility->camera.position - target));
        const float clear = ViewFraming::clearance(ctx, target) + 1.0f;
        ec.cameraController.viewAlongAxis(target, endpoints[hoverIdx].worldDir, dist, clear);
    }

    for (int i = 0; i < 6; ++i) {
        const Endpoint& e = endpoints[i];
        const bool isHovered = (i == hoverIdx);
        const bool isPositive = e.positive;

        if (isPositive) {
            drawList->AddLine(center, endPts[i], e.col, EditorStyle::px(2.0f));
        }

        // A negative end is faded and lettered only under the pointer.
        ImU32 dotCol = e.col;
        if (!isPositive) dotCol = (dotCol & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 110);
        if (isHovered)   dotCol = EditorStyle::HIGHLIGHT_U32;
        const float dotR = EditorStyle::px(isPositive || isHovered ? 8.0f : 6.0f);
        drawList->AddCircleFilled(endPts[i], dotR, dotCol, 16);

        if (isPositive || isHovered) {
            const ImVec2 ts = ImGui::CalcTextSize(e.label);
            const ImVec2 labelPos(endPts[i].x - ts.x * 0.5f, endPts[i].y - ts.y * 0.5f);
            drawList->AddText(labelPos, EditorStyle::NAV_LABEL_U32, e.label);
        }
    }

    if (hoverIdx >= 0) {
        m_hovered = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("View from %s, orthographic until turned", endpoints[hoverIdx].label);
    }
}

} // namespace Vkm::Engine
