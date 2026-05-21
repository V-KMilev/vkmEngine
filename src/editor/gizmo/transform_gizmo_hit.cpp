#include "gizmo/transform_gizmo.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

GizmoElement TransformGizmo::hitTestTranslation(const glm::vec3 axes[3], const ImVec2 screenAxes[3]) const {
    // Test plane quads first (they're smaller targets, higher priority)
    for (int i = 0; i < 3; ++i) {
        int a1 = (i + 1) % 3;
        int a2 = (i + 2) % 3;

        ImVec2 quadCorner(
            m_originScreen.x + (screenAxes[a1].x - m_originScreen.x) * PLANE_QUAD_FRAC
                             + (screenAxes[a2].x - m_originScreen.x) * PLANE_QUAD_FRAC,
            m_originScreen.y + (screenAxes[a1].y - m_originScreen.y) * PLANE_QUAD_FRAC
                             + (screenAxes[a2].y - m_originScreen.y) * PLANE_QUAD_FRAC
        );
        ImVec2 qA(
            m_originScreen.x + (screenAxes[a1].x - m_originScreen.x) * PLANE_QUAD_FRAC,
            m_originScreen.y + (screenAxes[a1].y - m_originScreen.y) * PLANE_QUAD_FRAC
        );
        ImVec2 qB(
            m_originScreen.x + (screenAxes[a2].x - m_originScreen.x) * PLANE_QUAD_FRAC,
            m_originScreen.y + (screenAxes[a2].y - m_originScreen.y) * PLANE_QUAD_FRAC
        );

        // Point-in-quad test using triangle method
        auto cross2D = [](ImVec2 o, ImVec2 a, ImVec2 b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };

        // Quad: origin, qA, quadCorner, qB
        ImVec2 pts[4] = { m_originScreen, qA, quadCorner, qB };
        bool inside = true;
        for (int j = 0; j < 4; ++j) {
            float c = cross2D(pts[j], pts[(j + 1) % 4], m_mousePos);
            if (c < 0.0f) { inside = false; break; }
        }
        if (!inside) {
            inside = true;
            for (int j = 0; j < 4; ++j) {
                float c = cross2D(pts[j], pts[(j + 1) % 4], m_mousePos);
                if (c > 0.0f) { inside = false; break; }
            }
        }

        if (inside) {
            static constexpr GizmoElement planes[] = {
                GizmoElement::PlaneYZ, GizmoElement::PlaneXZ, GizmoElement::PlaneXY
            };
            return planes[i];
        }
    }

    // Test axis lines
    float bestDist = AXIS_HIT_RADIUS + 1.0f;
    GizmoElement bestElem = GizmoElement::None;

    for (int i = 0; i < 3; ++i) {
        float d = distPointToSegment2D(m_mousePos, m_originScreen, screenAxes[i]);
        if (d < AXIS_HIT_RADIUS && d < bestDist) {
            bestDist = d;
            static constexpr GizmoElement axisElems[] = {
                GizmoElement::AxisX, GizmoElement::AxisY, GizmoElement::AxisZ
            };
            bestElem = axisElems[i];
        }
    }

    return bestElem;
}

GizmoElement TransformGizmo::hitTestRotation(const glm::vec3 axes[3]) const {
    glm::vec3 rayDir = screenToRay(m_mousePos);
    float ringRadius = m_screenFactor;

    float bestDist = AXIS_HIT_RADIUS + 1.0f;
    GizmoElement bestElem = GizmoElement::None;

    for (int i = 0; i < 3; ++i) {
        // Intersect ray with the axis plane
        float t = intersectRayPlane(m_cameraPos, rayDir, m_gizmoOrigin, axes[i]);
        if (t < 0.0f) continue;

        glm::vec3 hit = m_cameraPos + rayDir * t;
        float distFromCenter = glm::length(hit - m_gizmoOrigin);

        // Check if hit is near the ring
        float diff = std::abs(distFromCenter - ringRadius);
        // Convert world-space diff to screen pixels for threshold
        ImVec2 hitScreen = worldToScreen(hit);
        ImVec2 hitOffScreen = worldToScreen(hit + glm::normalize(hit - m_gizmoOrigin) * diff);
        float pixelDiff = std::sqrt(
            (hitScreen.x - hitOffScreen.x) * (hitScreen.x - hitOffScreen.x) +
            (hitScreen.y - hitOffScreen.y) * (hitScreen.y - hitOffScreen.y)
        );

        if (pixelDiff < AXIS_HIT_RADIUS && pixelDiff < bestDist) {
            bestDist = pixelDiff;
            static constexpr GizmoElement axisElems[] = {
                GizmoElement::AxisX, GizmoElement::AxisY, GizmoElement::AxisZ
            };
            bestElem = axisElems[i];
        }
    }

    return bestElem;
}

GizmoElement TransformGizmo::hitTestScale(const ImVec2 screenAxes[3]) const {
    // Same as translation axis test (lines) plus box handle at endpoints
    float bestDist = AXIS_HIT_RADIUS + 1.0f;
    GizmoElement bestElem = GizmoElement::None;

    for (int i = 0; i < 3; ++i) {
        // Check box handle at endpoint first
        float dx = m_mousePos.x - screenAxes[i].x;
        float dy = m_mousePos.y - screenAxes[i].y;
        float boxDist = std::max(std::abs(dx), std::abs(dy));
        if (boxDist < SCALE_BOX_HALF + 4.0f && boxDist < bestDist) {
            bestDist = boxDist;
            static constexpr GizmoElement axisElems[] = {
                GizmoElement::AxisX, GizmoElement::AxisY, GizmoElement::AxisZ
            };
            bestElem = axisElems[i];
            continue;
        }

        // Check axis line
        float d = distPointToSegment2D(m_mousePos, m_originScreen, screenAxes[i]);
        if (d < AXIS_HIT_RADIUS && d < bestDist) {
            bestDist = d;
            static constexpr GizmoElement axisElems[] = {
                GizmoElement::AxisX, GizmoElement::AxisY, GizmoElement::AxisZ
            };
            bestElem = axisElems[i];
        }
    }

    return bestElem;
}

} // namespace Engine
