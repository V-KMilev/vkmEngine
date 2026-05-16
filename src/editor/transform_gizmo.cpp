#include "transform_gizmo.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Engine {

ImVec2 TransformGizmo::worldToScreen(const glm::vec3& worldPos) const {
    glm::vec4 clip = m_viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 1e-7f) return ImVec2(-10000, -10000);

    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    float x = m_vpMin.x + (ndc.x * 0.5f + 0.5f) * m_vpWidth;
    float y = m_vpMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * m_vpHeight;
    return ImVec2(x, y);
}

glm::vec3 TransformGizmo::screenToRay(ImVec2 screenPos) const {
    float ndcX = ((screenPos.x - m_vpMin.x) / m_vpWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - ((screenPos.y - m_vpMin.y) / m_vpHeight) * 2.0f;

    glm::vec4 clipNear(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 clipFar(ndcX, ndcY, 1.0f, 1.0f);

    glm::vec4 worldNear = m_invViewProj * clipNear;
    glm::vec4 worldFar  = m_invViewProj * clipFar;
    worldNear /= worldNear.w;
    worldFar  /= worldFar.w;

    return glm::normalize(glm::vec3(worldFar - worldNear));
}

float TransformGizmo::computeScreenFactor(const glm::vec3& gizmoOrigin) const {
    // Project origin to clip space
    glm::vec4 clipOrigin = m_viewProj * glm::vec4(gizmoOrigin, 1.0f);
    if (clipOrigin.w <= 1e-7f) return 1.0f;

    // Camera right vector from inverse view
    glm::mat4 invView = glm::inverse(m_view);
    glm::vec3 cameraRight = glm::normalize(glm::vec3(invView[0]));

    glm::vec4 clipRight = m_viewProj * glm::vec4(gizmoOrigin + cameraRight, 1.0f);
    if (clipRight.w <= 1e-7f) return 1.0f;

    glm::vec2 ndcOrigin = glm::vec2(clipOrigin) / clipOrigin.w;
    glm::vec2 ndcRight  = glm::vec2(clipRight) / clipRight.w;
    float ndcLength = glm::length(ndcRight - ndcOrigin);
    if (ndcLength < 1e-7f) return 1.0f;

    // Desired size in NDC
    float ndcDesired = GIZMO_SIZE_PIXELS / (m_vpWidth * 0.5f);
    return ndcDesired / ndcLength;
}

float TransformGizmo::intersectRayPlane(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                         const glm::vec3& planePoint, const glm::vec3& planeNormal) {
    float denom = glm::dot(planeNormal, rayDir);
    if (std::abs(denom) < 1e-7f) return -1.0f;
    return glm::dot(planePoint - rayOrigin, planeNormal) / denom;
}

float TransformGizmo::distPointToSegment2D(ImVec2 p, ImVec2 a, ImVec2 b) {
    ImVec2 ab(b.x - a.x, b.y - a.y);
    ImVec2 ap(p.x - a.x, p.y - a.y);
    float abLen2 = ab.x * ab.x + ab.y * ab.y;
    if (abLen2 < 1e-7f) return std::sqrt(ap.x * ap.x + ap.y * ap.y);

    float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / abLen2, 0.0f, 1.0f);
    float dx = p.x - (a.x + t * ab.x);
    float dy = p.y - (a.y + t * ab.y);
    return std::sqrt(dx * dx + dy * dy);
}

glm::vec3 TransformGizmo::getAxisDirection(GizmoElement elem, const glm::vec3 axes[3]) const {
    switch (elem) {
        case GizmoElement::AxisX: return axes[0];
        case GizmoElement::AxisY: return axes[1];
        case GizmoElement::AxisZ: return axes[2];
        default: return glm::vec3(0.0f);
    }
}

glm::vec3 TransformGizmo::getDragPlaneNormal(GizmoElement elem, const glm::vec3 axes[3]) const {
    switch (elem) {
        case GizmoElement::AxisX: {
            glm::vec3 cross = glm::cross(m_cameraDir, axes[0]);
            if (glm::length(cross) < 1e-5f) return axes[1];
            return glm::normalize(glm::cross(axes[0], cross));
        }
        case GizmoElement::AxisY: {
            glm::vec3 cross = glm::cross(m_cameraDir, axes[1]);
            if (glm::length(cross) < 1e-5f) return axes[0];
            return glm::normalize(glm::cross(axes[1], cross));
        }
        case GizmoElement::AxisZ: {
            glm::vec3 cross = glm::cross(m_cameraDir, axes[2]);
            if (glm::length(cross) < 1e-5f) return axes[0];
            return glm::normalize(glm::cross(axes[2], cross));
        }
        case GizmoElement::PlaneYZ: return axes[0];
        case GizmoElement::PlaneXZ: return axes[1];
        case GizmoElement::PlaneXY: return axes[2];
        case GizmoElement::Screen:  return m_cameraDir;
        default: return m_cameraDir;
    }
}

ImU32 TransformGizmo::colorForElement(GizmoElement elem, GizmoElement highlight) const {
    if (elem == highlight) return COLOR_HIGHLIGHT;
    switch (elem) {
        case GizmoElement::AxisX: case GizmoElement::PlaneYZ: return COLOR_X;
        case GizmoElement::AxisY: case GizmoElement::PlaneXZ: return COLOR_Y;
        case GizmoElement::AxisZ: case GizmoElement::PlaneXY: return COLOR_Z;
        default: return IM_COL32(200, 200, 200, 200);
    }
}

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

// Drawing

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

// Drag Handling

bool TransformGizmo::handleTranslationDrag(glm::mat4& model, const glm::vec3 axes[3]) {
    glm::vec3 rayDir = screenToRay(m_mousePos);
    float t = intersectRayPlane(m_cameraPos, rayDir, m_dragPlanePoint, m_dragPlaneNormal);
    if (t < 0.0f) return false;

    glm::vec3 currentHit = m_cameraPos + rayDir * t;
    glm::vec3 delta = currentHit - m_dragStartWorldHit;

    // Constrain to axis if single-axis drag
    if (m_active == GizmoElement::AxisX || m_active == GizmoElement::AxisY || m_active == GizmoElement::AxisZ) {
        glm::vec3 axis = getAxisDirection(m_active, axes);
        delta = axis * glm::dot(delta, axis);
    }
    // For plane drags (PlaneXY, PlaneXZ, PlaneYZ), delta is already in the plane

    model = m_dragStartModel;
    model[3] = m_dragStartModel[3] + glm::vec4(delta, 0.0f);
    return true;
}

bool TransformGizmo::handleRotationDrag(glm::mat4& model, const glm::vec3 axes[3]) {
    glm::vec3 rayDir = screenToRay(m_mousePos);
    float t = intersectRayPlane(m_cameraPos, rayDir, m_dragPlanePoint, m_dragPlaneNormal);
    if (t < 0.0f) return false;

    glm::vec3 currentHit = m_cameraPos + rayDir * t;
    glm::vec3 currentDir = currentHit - m_gizmoOrigin;
    float currentLen = glm::length(currentDir);
    if (currentLen < 1e-6f) return false;
    currentDir /= currentLen;

    // Compute rotation angle
    float dotVal = std::clamp(glm::dot(m_rotationStartDir, currentDir), -1.0f, 1.0f);
    float crossDot = glm::dot(glm::cross(m_rotationStartDir, currentDir), m_rotationAxis);
    float angle = std::atan2(crossDot, dotVal);

    // Snap rotation angle directly (avoids Euler decomposition gimbal lock)
    if (m_snapAngle > 0.0f) {
        angle = std::round(angle / m_snapAngle) * m_snapAngle;
    }

    // Store delta rotation as quaternion for direct application
    m_dragRotation = glm::angleAxis(angle, m_rotationAxis);

    // Build rotation matrix around the axis through the gizmo origin
    glm::mat4 toOrigin = glm::translate(glm::mat4(1.0f), -m_gizmoOrigin);
    glm::mat4 fromOrigin = glm::translate(glm::mat4(1.0f), m_gizmoOrigin);
    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), angle, m_rotationAxis);

    model = fromOrigin * rot * toOrigin * m_dragStartModel;
    return true;
}

bool TransformGizmo::handleScaleDrag(glm::mat4& model, const glm::vec3 axes[3]) {
    glm::vec3 rayDir = screenToRay(m_mousePos);
    float t = intersectRayPlane(m_cameraPos, rayDir, m_dragPlanePoint, m_dragPlaneNormal);
    if (t < 0.0f) return false;

    glm::vec3 currentHit = m_cameraPos + rayDir * t;
    glm::vec3 axis = getAxisDirection(m_active, axes);
    float currentDist = glm::dot(currentHit - m_gizmoOrigin, axis);

    if (std::abs(m_scaleStartDist) < 1e-6f) return false;
    float scaleFactor = currentDist / m_scaleStartDist;
    scaleFactor = std::clamp(scaleFactor, 0.01f, 100.0f);

    // Decompose start model to apply scale to specific axis
    glm::vec3 startPos, startScale, startSkew;
    glm::vec4 startPersp;
    glm::quat startRot;
    glm::decompose(m_dragStartModel, startScale, startRot, startPos, startSkew, startPersp);

    // Determine which local axis to scale
    int axisIdx = (m_active == GizmoElement::AxisX) ? 0 :
                  (m_active == GizmoElement::AxisY) ? 1 : 2;

    glm::vec3 newScale = startScale;
    newScale[axisIdx] *= scaleFactor;

    // Reconstruct model matrix
    model = glm::translate(glm::mat4(1.0f), startPos)
          * glm::mat4_cast(glm::normalize(startRot))
          * glm::scale(glm::mat4(1.0f), newScale);
    return true;
}

bool TransformGizmo::manipulate(
    ImDrawList* drawList,
    const glm::mat4& view,
    const glm::mat4& projection,
    GizmoOperation operation,
    GizmoMode mode,
    glm::mat4& model,
    ImVec2 vpMin,
    float vpWidth,
    float vpHeight
) {
    // Cache per-frame state
    m_view = view;
    m_projection = projection;
    m_viewProj = projection * view;
    m_invViewProj = glm::inverse(m_viewProj);
    m_vpMin = vpMin;
    m_vpWidth = vpWidth;
    m_vpHeight = vpHeight;

    glm::mat4 invView = glm::inverse(view);
    m_cameraPos = glm::vec3(invView[3]);
    m_cameraDir = -glm::normalize(glm::vec3(invView[2]));

    m_gizmoOrigin = glm::vec3(model[3]);

    // Don't draw/interact when entity is behind camera
    glm::vec4 clipOrigin = m_viewProj * glm::vec4(m_gizmoOrigin, 1.0f);
    if (clipOrigin.w <= 0.0f) {
        m_hovered = GizmoElement::None;
        if (m_dragging) { m_dragging = false; m_active = GizmoElement::None; }
        return false;
    }

    m_screenFactor = computeScreenFactor(m_gizmoOrigin);
    m_originScreen = worldToScreen(m_gizmoOrigin);
    m_mousePos = ImGui::GetMousePos();

    // Compute gizmo axes (local or world)
    glm::vec3 axes[3];
    if (mode == GizmoMode::World) {
        axes[0] = glm::vec3(1, 0, 0);
        axes[1] = glm::vec3(0, 1, 0);
        axes[2] = glm::vec3(0, 0, 1);
    } else {
        // Extract orientation from model (normalize columns)
        axes[0] = glm::normalize(glm::vec3(model[0]));
        axes[1] = glm::normalize(glm::vec3(model[1]));
        axes[2] = glm::normalize(glm::vec3(model[2]));
    }

    // Project axis endpoints to screen
    ImVec2 screenAxes[3];
    for (int i = 0; i < 3; ++i) {
        screenAxes[i] = worldToScreen(m_gizmoOrigin + axes[i] * m_screenFactor);
    }

    // State machine
    bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool modified = false;

    if (m_dragging) {
        if (!mouseDown) {
            // Release
            m_dragging = false;
            m_active = GizmoElement::None;
            m_dragRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        } else {
            // Continue drag
            switch (operation) {
                case GizmoOperation::Translate: modified = handleTranslationDrag(model, axes); break;
                case GizmoOperation::Rotate:    modified = handleRotationDrag(model, axes);    break;
                case GizmoOperation::Scale:     modified = handleScaleDrag(model, axes);       break;
                case GizmoOperation::Select:    break;  // unreachable: skipped in GizmoOverlay
            }

            // Update origin and screen axes after model changes
            if (modified) {
                m_gizmoOrigin = glm::vec3(model[3]);
                m_originScreen = worldToScreen(m_gizmoOrigin);
                if (mode == GizmoMode::Local) {
                    axes[0] = glm::normalize(glm::vec3(model[0]));
                    axes[1] = glm::normalize(glm::vec3(model[1]));
                    axes[2] = glm::normalize(glm::vec3(model[2]));
                }
                for (int i = 0; i < 3; ++i) {
                    screenAxes[i] = worldToScreen(m_gizmoOrigin + axes[i] * m_screenFactor);
                }
            }
        }
    } else {
        // Check if mouse is in viewport region
        bool inViewport = m_mousePos.x >= m_vpMin.x && m_mousePos.x <= m_vpMin.x + m_vpWidth
                       && m_mousePos.y >= m_vpMin.y && m_mousePos.y <= m_vpMin.y + m_vpHeight;

        if (inViewport) {
            // Hit test
            switch (operation) {
                case GizmoOperation::Translate: m_hovered = hitTestTranslation(axes, screenAxes); break;
                case GizmoOperation::Rotate:    m_hovered = hitTestRotation(axes);                break;
                case GizmoOperation::Scale:     m_hovered = hitTestScale(screenAxes);             break;
                case GizmoOperation::Select:    m_hovered = GizmoElement::None;                   break;
            }
        } else {
            m_hovered = GizmoElement::None;
        }

        // Start drag
        if (m_hovered != GizmoElement::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_active = m_hovered;
            m_dragging = true;
            m_dragStartModel = model;
            m_dragPlaneNormal = getDragPlaneNormal(m_active, axes);
            m_dragPlanePoint = m_gizmoOrigin;

            glm::vec3 rayDir = screenToRay(m_mousePos);
            float t = intersectRayPlane(m_cameraPos, rayDir, m_dragPlanePoint, m_dragPlaneNormal);
            if (t > 0.0f) {
                m_dragStartWorldHit = m_cameraPos + rayDir * t;
            } else {
                m_dragStartWorldHit = m_gizmoOrigin;
            }

            // Rotation-specific init
            if (operation == GizmoOperation::Rotate) {
                m_rotationAxis = getAxisDirection(m_active, axes);
                // For rotation, the plane normal IS the rotation axis
                m_dragPlaneNormal = m_rotationAxis;
                float t2 = intersectRayPlane(m_cameraPos, rayDir, m_dragPlanePoint, m_dragPlaneNormal);
                if (t2 > 0.0f) {
                    glm::vec3 hit = m_cameraPos + rayDir * t2;
                    m_rotationStartDir = glm::normalize(hit - m_gizmoOrigin);
                } else {
                    m_rotationStartDir = glm::vec3(1, 0, 0);
                }
            }

            // Scale-specific init
            if (operation == GizmoOperation::Scale) {
                glm::vec3 axis = getAxisDirection(m_active, axes);
                m_scaleStartDist = glm::dot(m_dragStartWorldHit - m_gizmoOrigin, axis);
                if (std::abs(m_scaleStartDist) < 1e-6f) m_scaleStartDist = 1.0f;
            }
        }
    }

    // draw gizmo
    drawList->PushClipRect(
        ImVec2(m_vpMin.x, m_vpMin.y),
        ImVec2(m_vpMin.x + m_vpWidth, m_vpMin.y + m_vpHeight),
        true
    );

    switch (operation) {
        case GizmoOperation::Translate: drawTranslationGizmo(drawList, screenAxes);  break;
        case GizmoOperation::Rotate:    drawRotationGizmo(drawList, axes);           break;
        case GizmoOperation::Scale:     drawScaleGizmo(drawList, screenAxes);        break;
        case GizmoOperation::Select:    break;  // unreachable: skipped in GizmoOverlay
    }

    drawList->PopClipRect();

    return modified;
}

} // namespace Engine
