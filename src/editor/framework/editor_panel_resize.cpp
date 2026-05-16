#include "framework/editor_panel_resize.h"
#include "framework/editor_state.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Engine {

void EditorPanelResize::process(EditorState& state, const Layout& L, bool blockNew) {
    constexpr float RESIZE_ZONE = 4.0f;

    ImVec2 mpos      = ImGui::GetMousePos();
    bool   mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    ImVec2 delta     = ImGui::GetIO().MouseDelta;
    bool   alreadyResizing = m_resizingLeft || m_resizingRight || m_resizingBottom;
    // Block a *new* resize if an ImGui widget or the gizmo is active.
    bool   canStartNew = !ImGui::IsAnyItemActive() && !blockNew && !alreadyResizing;

    auto handleEdge = [&](bool show, float edgePos, bool horizontal,
                          bool& resizingFlag, float& panelSize, float sign,
                          float minSize, float maxSize) {
        if (!show) return;
        bool nearEdge;
        if (horizontal) {
            nearEdge = std::abs(mpos.y - edgePos) <= RESIZE_ZONE
                    && mpos.x >= L.areaStart.x
                    && mpos.x <= L.areaStart.x + L.workW;
        } else {
            nearEdge = std::abs(mpos.x - edgePos) <= RESIZE_ZONE
                    && mpos.y >= L.areaStart.y
                    && mpos.y <= L.areaStart.y + L.mainH;
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

    handleEdge(L.showLeft, L.areaStart.x + L.leftW, false,
               m_resizingLeft, state.leftPanelWidth, 1.0f, 180.0f, 500.0f);
    handleEdge(L.showRight, L.areaStart.x + L.workW - L.rightW, false,
               m_resizingRight, state.rightPanelWidth, -1.0f, 240.0f, 600.0f);
    handleEdge(L.showBottom, L.areaStart.y + L.mainH, true,
               m_resizingBottom, state.bottomPanelHeight, -1.0f, 100.0f, 500.0f);

    if (!mouseDown) {
        m_resizingLeft = m_resizingRight = m_resizingBottom = false;
    }
}

} // namespace Engine
