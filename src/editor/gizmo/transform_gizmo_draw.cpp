#include "gizmo/transform_gizmo.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

void TransformGizmo::drawTranslationGizmo(ImDrawList* dl, const ImVec2 screenAxes[3]) {
    GizmoElement hl = m_dragging ? m_active : m_hovered;

    // Draw plane quads
    for (int i = 0; i < 3; ++i) {
        ImVec2 qA, qB, qC;
        planeQuadCorners(i, screenAxes, qA, qB, qC);

        static constexpr ImU32 planeFills[] = { COLOR_PLANE_X, COLOR_PLANE_Y, COLOR_PLANE_Z };
        const ImU32 fillColor = (GIZMO_PLANES[i] == hl)
            ? IM_COL32(255, 210, 50, 60)   // highlight, semi-transparent
            : planeFills[i];

        dl->AddQuadFilled(m_originScreen, qA, qC, qB, fillColor);
    }

    // Draw axis lines and arrow heads
    for (int i = 0; i < 3; ++i) {
        ImU32 col = colorForElement(GIZMO_AXES[i], hl);
        float thick = (GIZMO_AXES[i] == hl) ? HIGHLIGHT_THICKNESS : LINE_THICKNESS;

        dl->AddLine(m_originScreen, screenAxes[i], col, thick * m_uiScale);

        // Arrow head (triangle)
        ImVec2 dir(screenAxes[i].x - m_originScreen.x, screenAxes[i].y - m_originScreen.y);
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 1.0f) {
            dir.x /= len;
            dir.y /= len;
            ImVec2 perp(-dir.y, dir.x);
            float headSize = ARROW_HEAD_PIXELS * m_uiScale;
            ImVec2 tip = screenAxes[i];
            ImVec2 base1(tip.x - dir.x * headSize * 2.0f + perp.x * headSize,
                         tip.y - dir.y * headSize * 2.0f + perp.y * headSize);
            ImVec2 base2(tip.x - dir.x * headSize * 2.0f - perp.x * headSize,
                         tip.y - dir.y * headSize * 2.0f - perp.y * headSize);
            dl->AddTriangleFilled(tip, base1, base2, col);
        }
    }

    // Center dot
    dl->AddCircleFilled(m_originScreen, 3.0f * m_uiScale, IM_COL32(255, 255, 255, 200), 8);
}

void TransformGizmo::drawRotationGizmo(ImDrawList* dl, const glm::vec3 axes[3]) {
    GizmoElement hl = m_dragging ? m_active : m_hovered;
    float radius = m_screenFactor;

    const float angleStep = 2.0f * glm::pi<float>() / CIRCLE_SEGMENTS;
    const float halfStep  = angleStep * 0.5f;

    for (int i = 0; i < 3; ++i) {
        ImU32 col = colorForElement(GIZMO_AXES[i], hl);
        float thick = ((GIZMO_AXES[i] == hl) ? HIGHLIGHT_THICKNESS : LINE_THICKNESS) * m_uiScale;

        // Generate circle points on the plane perpendicular to this axis
        glm::vec3 normal = axes[i];

        // Find two vectors perpendicular to the normal
        glm::vec3 tangent;
        if (std::abs(glm::dot(normal, glm::vec3(0, 1, 0))) < 0.99f) {
            tangent = glm::normalize(glm::cross(normal, glm::vec3(0, 1, 0)));
        } else {
            tangent = glm::normalize(glm::cross(normal, glm::vec3(1, 0, 0)));
        }
        glm::vec3 bitangent = glm::cross(normal, tangent);

        ImVec2 prevPt{};
        for (int s = 0; s <= CIRCLE_SEGMENTS; ++s) {
            float angle = s * angleStep;
            glm::vec3 worldPt = m_gizmoOrigin
                + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
            ImVec2 pt = worldToScreen(worldPt);

            if (s > 0) {
                // Only draw segments facing the camera (back-face culling for
                // rings); the facing test samples the segment's midpoint.
                glm::vec3 midWorld = m_gizmoOrigin
                    + (tangent * std::cos(angle - halfStep)
                       + bitangent * std::sin(angle - halfStep)) * radius;
                glm::vec3 toCamera = glm::normalize(m_cameraPos - midWorld);
                float faceDot = glm::dot(toCamera, normal);

                // Draw if facing camera (dot > 0) or if this axis is highlighted
                if (std::abs(faceDot) > 0.05f || GIZMO_AXES[i] == hl) {
                    float alpha = std::abs(faceDot);
                    alpha = std::clamp(alpha * 3.0f, 0.2f, 1.0f);
                    // Modulate color alpha
                    ImU32 segCol = col;
                    if (GIZMO_AXES[i] != hl) {
                        uint8_t a = static_cast<uint8_t>(255.0f * alpha);
                        segCol = (col & 0x00FFFFFF) | (static_cast<ImU32>(a) << 24);
                    }
                    dl->AddLine(prevPt, pt, segCol, thick);
                }
            }
            prevPt = pt;
        }
    }

    // Center dot
    dl->AddCircleFilled(m_originScreen, 3.0f * m_uiScale, IM_COL32(255, 255, 255, 200), 8);
}

void TransformGizmo::drawScaleGizmo(ImDrawList* dl, const ImVec2 screenAxes[3]) {
    GizmoElement hl = m_dragging ? m_active : m_hovered;
    const float scale = m_uiScale;

    for (int i = 0; i < 3; ++i) {
        ImU32 col = colorForElement(GIZMO_AXES[i], hl);
        float thick = ((GIZMO_AXES[i] == hl) ? HIGHLIGHT_THICKNESS : LINE_THICKNESS) * scale;

        dl->AddLine(m_originScreen, screenAxes[i], col, thick);

        // Box handle at endpoint
        const float half = SCALE_BOX_HALF * scale;
        ImVec2 boxMin(screenAxes[i].x - half, screenAxes[i].y - half);
        ImVec2 boxMax(screenAxes[i].x + half, screenAxes[i].y + half);
        dl->AddRectFilled(boxMin, boxMax, col);
    }
}

} // namespace Engine
