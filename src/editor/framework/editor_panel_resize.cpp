#include "framework/editor_panel_resize.h"
#include "framework/editor_state.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Engine {

void EditorPanelResize::process(
    EditorState& state,
    ImVec2 areaStart,
    float mainH,
    float workW,
    bool blockNew
) {
    constexpr float RESIZE_ZONE = 4.0f;

    const ImVec2 mpos      = ImGui::GetMousePos();
    const bool   mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const ImVec2 delta     = ImGui::GetIO().MouseDelta;
    const bool   alreadyResizing = m_resizingLeft || m_resizingRight || m_resizingBottom;
    // Block a *new* resize if an ImGui widget or the gizmo is active.
    const bool   canStartNew = !ImGui::IsAnyItemActive() && !blockNew && !alreadyResizing;

    // Mirror the visibility checks that drawWorkspace uses, so the resizer
    // sees the same panels the user sees.
    const float leftW  = state.showHierarchy ? state.leftPanelWidth  : 0.0f;
    const float rightW = state.showInspector ? state.rightPanelWidth : 0.0f;

    auto handleEdge = [&](bool show, float edgePos, bool horizontal,
                          bool& resizingFlag, float& panelSize, float sign,
                          float minSize, float maxSize) {
        if (!show) return;
        bool nearEdge;
        if (horizontal) {
            nearEdge = std::abs(mpos.y - edgePos) <= RESIZE_ZONE
                    && mpos.x >= areaStart.x
                    && mpos.x <= areaStart.x + workW;
        } else {
            nearEdge = std::abs(mpos.x - edgePos) <= RESIZE_ZONE
                    && mpos.y >= areaStart.y
                    && mpos.y <= areaStart.y + mainH;
        }

        // Continue an existing resize drag
        if (resizingFlag) {
            ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
            float d = horizontal ? delta.y : delta.x;
            panelSize += d * sign;
            panelSize = std::clamp(panelSize, minSize, maxSize);
            return;
        }

        // Show cursor hint when hovering (even if can't start)
        if (nearEdge) {
            ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
        }

        // Only start a new resize on click when nothing else is active
        if (nearEdge && canStartNew && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            resizingFlag = true;
        }
    };

    // The horizontal max must leave room for both the other side panel
    // (if shown) AND a minimum viewport in the middle - clamping each
    // panel against its own static max only would let the viewport
    // collapse to nothing.
    constexpr float MIN_CENTER  = 200.0f;
    constexpr float LEFT_MIN    = 180.0f;
    constexpr float LEFT_MAX    = 500.0f;
    constexpr float RIGHT_MIN   = 240.0f;
    constexpr float RIGHT_MAX   = 600.0f;
    const float leftMax  = std::min(LEFT_MAX,  workW - rightW - MIN_CENTER);
    const float rightMax = std::min(RIGHT_MAX, workW - leftW  - MIN_CENTER);

    handleEdge(state.showHierarchy, areaStart.x + leftW, false,
               m_resizingLeft, state.leftPanelWidth, 1.0f, LEFT_MIN, std::max(LEFT_MIN, leftMax));
    handleEdge(state.showInspector, areaStart.x + workW - rightW, false,
               m_resizingRight, state.rightPanelWidth, -1.0f, RIGHT_MIN, std::max(RIGHT_MIN, rightMax));
    handleEdge(state.showBottom, areaStart.y + mainH, true,
               m_resizingBottom, state.bottomPanelHeight, -1.0f, 100.0f, 500.0f);

    if (!mouseDown) {
        m_resizingLeft = m_resizingRight = m_resizingBottom = false;
    }
}

} // namespace Engine
