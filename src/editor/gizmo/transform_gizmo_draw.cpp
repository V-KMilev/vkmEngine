#include "gizmo/transform_gizmo.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

void TransformGizmo::drawTranslationGizmo(ImDrawList* dl, const ImVec2 screenAxes[3]) {
    GizmoElement hl = m_dragging ? m_active : m_hovered;

    // Draw plane quads
    for (int i = 0; i < 3; ++i) {
        int a1 = (i + 1) % 3;
        int a2 = (i + 2) % 3;

        ImVec2 qA(
            m_originScreen.x + (screenAxes[a1].x - m_originScreen.x) * PLANE_QUAD_FRAC,
            m_originScreen.y + (screenAxes[a1].y - m_originScreen.y) * PLANE_QUAD_FRAC
        );
        ImVec2 qB(
            m_originScreen.x + (screenAxes[a2].x - m_originScreen.x) * PLANE_QUAD_FRAC,
            m_originScreen.y + (screenAxes[a2].y - m_originScreen.y) * PLANE_QUAD_FRAC
        );
        ImVec2 qC(
            m_originScreen.x + (screenAxes[a1].x - m_originScreen.x) * PLANE_QUAD_FRAC
                             + (screenAxes[a2].x - m_originScreen.x) * PLANE_QUAD_FRAC,
            m_originScreen.y + (screenAxes[a1].y - m_originScreen.y) * PLANE_QUAD_FRAC
                             + (screenAxes[a2].y - m_originScreen.y) * PLANE_QUAD_FRAC
        );

        static constexpr GizmoElement planes[] = {
            GizmoElement::PlaneYZ, GizmoElement::PlaneXZ, GizmoElement::PlaneXY
        };
        ImU32 fillColor = colorForElement(planes[i], hl);
        // Make fill semi-transparent
        if (planes[i] != hl) {
            static constexpr ImU32 planeFills[] = { COLOR_PLANE_X, COLOR_PLANE_Y, COLOR_PLANE_Z };
            fillColor = planeFills[i];
        } else {
            fillColor = IM_COL32(255, 210, 50, 60);
        }

        dl->AddQuadFilled(m_originScreen, qA, qC, qB, fillColor);
    }

    // Draw axis lines and arrow heads
    for (int i = 0; i < 3; ++i) {
        static constexpr GizmoElement axisElems[] = {
            GizmoElement::AxisX, GizmoElement::AxisY, GizmoElement::AxisZ
        };
        ImU32 col = colorForElement(axisElems[i], hl);
        float thick = (axisElems[i] == hl) ? HIGHLIGHT_THICKNESS : LINE_THICKNESS;

        dl->AddLine(m_originScreen, screenAxes[i], col, thick);

        // Arrow head (triangle)
        ImVec2 dir(screenAxes[i].x - m_originScreen.x, screenAxes[i].y - m_originScreen.y);
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 1.0f) {
            dir.x /= len;
            dir.y /= len;
            ImVec2 perp(-dir.y, dir.x);
            float headSize = ARROW_HEAD_PIXELS;
            ImVec2 tip = screenAxes[i];
            ImVec2 base1(tip.x - dir.x * headSize * 2.0f + perp.x * headSize,
                         tip.y - dir.y * headSize * 2.0f + perp.y * headSize);
            ImVec2 base2(tip.x - dir.x * headSize * 2.0f - perp.x * headSize,
                         tip.y - dir.y * headSize * 2.0f - perp.y * headSize);
            dl->AddTriangleFilled(tip, base1, base2, col);
        }
    }

    // Center dot
    dl->AddCircleFilled(m_originScreen, 3.0f, IM_COL32(255, 255, 255, 200), 8);
}

void TransformGizmo::drawRotationGizmo(ImDrawList* dl, const glm::vec3 axes[3]) {
    GizmoElement hl = m_dragging ? m_active : m_hovered;
    float radius = m_screenFactor;

    for (int i = 0; i < 3; ++i) {
        static constexpr GizmoElement axisElems[] = {
            GizmoElement::AxisX, GizmoElement::AxisY, GizmoElement::AxisZ
        };
        ImU32 col = colorForElement(axisElems[i], hl);
        float thick = (axisElems[i] == hl) ? HIGHLIGHT_THICKNESS : LINE_THICKNESS;

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
            float angle = (static_cast<float>(s) / static_cast<float>(CIRCLE_SEGMENTS)) * 2.0f * glm::pi<float>();
            glm::vec3 worldPt = m_gizmoOrigin
                + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
            ImVec2 pt = worldToScreen(worldPt);

            if (s > 0) {
                // Only draw segments facing the camera (back-face culling for rings)
                glm::vec3 midWorld = m_gizmoOrigin
                    + (tangent * std::cos(angle - 0.5f * 2.0f * glm::pi<float>() / CIRCLE_SEGMENTS)
                       + bitangent * std::sin(angle - 0.5f * 2.0f * glm::pi<float>() / CIRCLE_SEGMENTS))
                    * radius;
                glm::vec3 toCamera = glm::normalize(m_cameraPos - midWorld);
                float faceDot = glm::dot(toCamera, normal);

                // Draw if facing camera (dot > 0) or if this axis is highlighted
                if (std::abs(faceDot) > 0.05f || axisElems[i] == hl) {
                    float alpha = std::abs(faceDot);
                    alpha = std::clamp(alpha * 3.0f, 0.2f, 1.0f);
                    // Modulate color alpha
                    ImU32 segCol = col;
                    if (axisElems[i] != hl) {
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
    dl->AddCircleFilled(m_originScreen, 3.0f, IM_COL32(255, 255, 255, 200), 8);
}

void TransformGizmo::drawScaleGizmo(ImDrawList* dl, const ImVec2 screenAxes[3]) {
    GizmoElement hl = m_dragging ? m_active : m_hovered;

    for (int i = 0; i < 3; ++i) {
        static constexpr GizmoElement axisElems[] = {
            GizmoElement::AxisX, GizmoElement::AxisY, GizmoElement::AxisZ
        };
        ImU32 col = colorForElement(axisElems[i], hl);
        float thick = (axisElems[i] == hl) ? HIGHLIGHT_THICKNESS : LINE_THICKNESS;

        dl->AddLine(m_originScreen, screenAxes[i], col, thick);

        // Box handle at endpoint
        ImVec2 boxMin(screenAxes[i].x - SCALE_BOX_HALF, screenAxes[i].y - SCALE_BOX_HALF);
        ImVec2 boxMax(screenAxes[i].x + SCALE_BOX_HALF, screenAxes[i].y + SCALE_BOX_HALF);
        dl->AddRectFilled(boxMin, boxMax, col);
    }

    // Center box (uniform scale)
    float centerSize = 3.5f;
    dl->AddRectFilled(
        ImVec2(m_originScreen.x - centerSize, m_originScreen.y - centerSize),
        ImVec2(m_originScreen.x + centerSize, m_originScreen.y + centerSize),
        IM_COL32(255, 255, 255, 200)
    );
}

} // namespace Engine
