#include "overlays/navigation_axes.h"

#include <algorithm>
#include <limits>

#include <imgui.h>
#include <glm/glm.hpp>

#include "core/frame_context.h"
#include "ecs/component/scene/transform.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "ui/style/editor_style.h"
#include "base/math/axes.h"
#include "ecs/component/scene/world_transform.h"
#include "core/frame/visibility.h"
#include "view/camera_controller_system.h"
#include "view/view_framing.h"

namespace Vkm::Engine {

namespace {

constexpr ImU32 NAV_DISC_U32 = IM_COL32(20, 20, 22, 160);
constexpr ImU32 NAV_RING_U32 = IM_COL32(50, 50, 55, 200);
constexpr ImU32 NAV_LABEL_U32 = IM_COL32(16, 16, 20, 255);  // letter on an axis dot

// Shared by drawing the axes and reserving their room.
float discSize()       { return EditorStyle::px(64.0f); }
float axisLength()     { return discSize() * 0.8f; }
float endpointRadius() { return EditorStyle::px(8.0f); }
float edgeInset()      { return axisLength() + EditorStyle::px(12.0f); }

} // namespace

float NavigationAxes::reach() {
    return edgeInset() + axisLength() + endpointRadius();
}

void NavigationAxes::drawNavigationGizmo(EditorContext& ec, bool shown) {
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
    glm::vec3 axisX   = viewRot * Math::WORLD_RIGHT;
    glm::vec3 axisY   = viewRot * Math::WORLD_UP;
    glm::vec3 axisZ   = viewRot * -Math::WORLD_FORWARD;

    const float axisLen        = axisLength();
    const float labelDotRadius = endpointRadius();

    struct Endpoint {
        glm::vec3   worldDir;  ///< The direction CameraControllerSystem::viewFrom takes, world space.
        glm::vec3   viewDir;   ///< View-rotated, for the depth sort and the screen position.
        ImU32       col;
        const char* label;
        bool        positive;
    };
    Endpoint endpoints[] = {
        {  Math::WORLD_RIGHT,    axisX, EditorStyle::AXIS_X_U32, "X",  true  },
        { -Math::WORLD_RIGHT,   -axisX, EditorStyle::AXIS_X_U32, "-X", false },
        {  Math::WORLD_UP,       axisY, EditorStyle::AXIS_Y_U32, "Y",  true  },
        { -Math::WORLD_UP,      -axisY, EditorStyle::AXIS_Y_U32, "-Y", false },
        { -Math::WORLD_FORWARD,  axisZ, EditorStyle::AXIS_Z_U32, "Z",  true  },
        {  Math::WORLD_FORWARD, -axisZ, EditorStyle::AXIS_Z_U32, "-Z", false },
    };

    // Back-to-front: the view looks down -Z, so the smallest z is drawn first.
    std::sort(
        std::begin(endpoints),
        std::end(endpoints),
        [](const Endpoint& a, const Endpoint& b) { return a.viewDir.z < b.viewDir.z; }
    );

    // Front-most endpoint (largest view z) wins. Skipped when the pointer is not the
    // viewport's, or the gizmo would light up while hovering the Inspector.
    const ImVec2 mp  = ImGui::GetMousePos();
    int hoverIdx     = -1;
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
            hoverIdx   = i;
        }
    }

    // The disc shows only while the pointer is near; at rest it would clutter the scene.
    const float hoverRadius = axisLen + labelDotRadius;
    const float nx          = mp.x - center.x, ny = mp.y - center.y;
    if (ec.input.navigationMayHover() && nx * nx + ny * ny <= hoverRadius * hoverRadius) {
        drawList->AddCircleFilled(center, gizmoSize * 0.5f, NAV_DISC_U32, 32);
        drawList->AddCircle(center, gizmoSize * 0.5f, NAV_RING_U32, 32, EditorStyle::px(1.0f));
    }

    if (hoverIdx >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        glm::vec3 target(0.0f);
        const EditorState& state = ec.state;
        // World pose: a parented entity's local position would orbit an empty point.
        if (const Transform* at = ctx.scene.tryGet<Transform>(state.selection.active())) {
            target = resolvedWorldPosition(ctx.scene, state.selection.active(), *at);
        }
        const float dist  = std::max(2.0f, glm::length(ctx.visibility->camera.position - target));
        const float clear = ViewFraming::clearance(ctx, target) + 1.0f;
        ec.cameraController.viewAlongAxis(target, endpoints[hoverIdx].worldDir, dist, clear);
    }

    for (int i = 0; i < 6; ++i) {
        const Endpoint& e     = endpoints[i];
        const bool isHovered  = (i == hoverIdx);
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
            drawList->AddText(labelPos, NAV_LABEL_U32, e.label);
        }
    }

    if (hoverIdx >= 0) {
        m_hovered = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("View from %s, orthographic until turned", endpoints[hoverIdx].label);
    }
}

} // namespace Vkm::Engine
