#include "gizmo/transform_gizmo.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

GizmoElement TransformGizmo::hitTestTranslation(const glm::vec3 axes[3], const ImVec2 screenAxes[3]) const {
    // Test plane quads first (they're smaller targets, higher priority)
    for (int i = 0; i < 3; ++i) {
        ImVec2 qA, qB, qC;
        planeQuadCorners(i, screenAxes, qA, qB, qC);

        auto cross2D = [](ImVec2 o, ImVec2 a, ImVec2 b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };

        // Point-in-quad: the mouse is inside when every edge cross product
        // has the same sign (the quad's screen winding flips with the view,
        // so either all-positive or all-negative counts as inside).
        ImVec2 pts[4] = { m_originScreen, qA, qC, qB };
        bool hasNeg = false;
        bool hasPos = false;
        for (int j = 0; j < 4; ++j) {
            const float c = cross2D(pts[j], pts[(j + 1) % 4], m_mousePos);
            hasNeg |= c < 0.0f;
            hasPos |= c > 0.0f;
        }
        if (!(hasNeg && hasPos)) return GIZMO_PLANES[i];
    }

    // Test axis lines
    float bestDist = (AXIS_HIT_RADIUS * m_uiScale) + 1.0f;
    GizmoElement bestElem = GizmoElement::None;

    for (int i = 0; i < 3; ++i) {
        float d = distPointToSegment2D(m_mousePos, m_originScreen, screenAxes[i]);
        if (d < (AXIS_HIT_RADIUS * m_uiScale) && d < bestDist) {
            bestDist = d;
            bestElem = GIZMO_AXES[i];
        }
    }

    return bestElem;
}

GizmoElement TransformGizmo::hitTestRotation(const glm::vec3 axes[3]) const {
    glm::vec3 rayDir = screenToRay(m_mousePos);
    float ringRadius = m_screenFactor;

    float bestDist = (AXIS_HIT_RADIUS * m_uiScale) + 1.0f;
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

        if (pixelDiff < (AXIS_HIT_RADIUS * m_uiScale) && pixelDiff < bestDist) {
            bestDist = pixelDiff;
            bestElem = GIZMO_AXES[i];
        }
    }

    return bestElem;
}

GizmoElement TransformGizmo::hitTestScale(const ImVec2 screenAxes[3]) const {
    // Same as translation axis test (lines) plus box handle at endpoints
    float bestDist = (AXIS_HIT_RADIUS * m_uiScale) + 1.0f;
    GizmoElement bestElem = GizmoElement::None;

    for (int i = 0; i < 3; ++i) {
        // Check box handle at endpoint first
        float dx = m_mousePos.x - screenAxes[i].x;
        float dy = m_mousePos.y - screenAxes[i].y;
        float boxDist = std::max(std::abs(dx), std::abs(dy));
        if (boxDist < (SCALE_BOX_HALF * m_uiScale) + 4.0f && boxDist < bestDist) {
            bestDist = boxDist;
            bestElem = GIZMO_AXES[i];
            continue;
        }

        // Check axis line
        float d = distPointToSegment2D(m_mousePos, m_originScreen, screenAxes[i]);
        if (d < (AXIS_HIT_RADIUS * m_uiScale) && d < bestDist) {
            bestDist = d;
            bestElem = GIZMO_AXES[i];
        }
    }

    return bestElem;
}

} // namespace Engine
