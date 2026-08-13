#include "framework/editor_panel_resize.h"

#include "ui/editor_style.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "framework/editor_state.h"

namespace Engine {

void EditorPanelResize::process(
    EditorState& state,
    ImVec2 areaStart,
    float mainH,
    float workW,
    bool blockNew
) {
    const float RESIZE_ZONE = EditorStyle::px(4.0f);

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

        // The seam lights up under the cursor / while dragging - the mouse
        // cursor alone was the only affordance that the edge is grabbable.
        auto drawSeam = [&](bool active) {
            const float half = EditorStyle::px(1.0f);
            ImVec2 a, b;
            if (horizontal) {
                a = ImVec2(areaStart.x,         edgePos - half);
                b = ImVec2(areaStart.x + workW, edgePos + half);
            } else {
                a = ImVec2(edgePos - half, areaStart.y);
                b = ImVec2(edgePos + half, areaStart.y + mainH);
            }
            ImVec4 col = active ? EditorStyle::ACCENT : EditorStyle::ACCENT_HOV;
            col.w = active ? 0.9f : 0.5f;
            ImGui::GetForegroundDrawList()->AddRectFilled(a, b, ImGui::GetColorU32(col));
        };

        // Continue an existing resize drag
        if (resizingFlag) {
            ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
            drawSeam(true);
            float d = horizontal ? delta.y : delta.x;
            panelSize += d * sign;
            panelSize = std::clamp(panelSize, minSize, maxSize);
            return;
        }

        // Show the hint when hovering (even if a drag can't start)
        if (nearEdge) {
            ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
            drawSeam(false);
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
    const float MIN_CENTER  = EditorStyle::px(200.0f);
    const float LEFT_MIN    = EditorStyle::px(180.0f);
    const float LEFT_MAX    = EditorStyle::px(500.0f);
    const float RIGHT_MIN   = EditorStyle::px(240.0f);
    const float RIGHT_MAX   = EditorStyle::px(600.0f);
    const float BOTTOM_MIN  = EditorStyle::px(100.0f);
    const float BOTTOM_MAX  = EditorStyle::px(500.0f);
    const float leftMax  = std::min(LEFT_MAX,  workW - rightW - MIN_CENTER);
    const float rightMax = std::min(RIGHT_MAX, workW - leftW  - MIN_CENTER);

    // Re-clamp the stored sizes every frame, not just during drags - an OS
    // window shrink could otherwise leave the panels wider than the work area
    // and drive the center viewport width negative.
    state.leftPanelWidth   = std::clamp(state.leftPanelWidth,   LEFT_MIN,  std::max(LEFT_MIN,  leftMax));
    state.rightPanelWidth  = std::clamp(state.rightPanelWidth,  RIGHT_MIN, std::max(RIGHT_MIN, rightMax));
    state.bottomPanelHeight = std::clamp(state.bottomPanelHeight, BOTTOM_MIN, BOTTOM_MAX);

    handleEdge(state.showHierarchy, areaStart.x + leftW, false,
               m_resizingLeft, state.leftPanelWidth, 1.0f, LEFT_MIN, std::max(LEFT_MIN, leftMax));
    handleEdge(state.showInspector, areaStart.x + workW - rightW, false,
               m_resizingRight, state.rightPanelWidth, -1.0f, RIGHT_MIN, std::max(RIGHT_MIN, rightMax));
    handleEdge(state.showBottom, areaStart.y + mainH, true,
               m_resizingBottom, state.bottomPanelHeight, -1.0f, BOTTOM_MIN, BOTTOM_MAX);

    if (!mouseDown) {
        m_resizingLeft = m_resizingRight = m_resizingBottom = false;
    }
}

} // namespace Engine
