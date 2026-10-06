#include "overlays/transform_gizmo.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "overlays/wire_draw.h"

namespace Vkm::Engine {

namespace {
// A drag plane needs an axis the camera is not looking along. Below this the
// cross product of the two is noise, and the plane it defines flips from one
// side of the axis to the other under the mouse.
constexpr float MIN_DRAG_PLANE_CROSS = 1e-5f;

// How edge-on a rotation ring may turn before it stops being drawn - and
// therefore before it stops being clickable. Shared by the draw and the hit
// test so the two cannot disagree about which half of the ring is there.
constexpr float RING_FACING_MIN = 0.05f;
} // namespace

bool TransformGizmo::project(const glm::vec3& worldPos, ImVec2& out) const {
    return projectToViewport(m_viewProj, worldPos, m_vpMin, ImVec2(m_vpWidth, m_vpHeight), out);
}

Math::Ray TransformGizmo::screenToRay(ImVec2 screenPos) const {
    return viewportRay(m_invViewProj, screenPos, m_vpMin, ImVec2(m_vpWidth, m_vpHeight));
}

glm::vec3 TransformGizmo::toViewer(const glm::vec3& worldPos) const {
    const glm::vec4 clip = m_viewProj * glm::vec4(worldPos, 1.0f);
    return -Math::rayThroughNdc(m_invViewProj, glm::vec2(clip) / clip.w).direction;
}

float TransformGizmo::computeScreenFactor(const glm::vec3& gizmoOrigin) const {
    glm::vec4 clipOrigin = m_viewProj * glm::vec4(gizmoOrigin, 1.0f);
    if (nearPlaneSide(clipOrigin) <= 0.0f) return 1.0f;

    // The sample point is a world unit sideways, so it can be behind the plane
    // while the origin is in front of it - close in, that is the one whose
    // divisor blows the measured length up and the gizmo with it.
    glm::vec4 clipRight = m_viewProj * glm::vec4(gizmoOrigin + m_cameraRight, 1.0f);
    if (nearPlaneSide(clipRight) <= 0.0f) return 1.0f;

    glm::vec2 ndcOrigin = glm::vec2(clipOrigin) / clipOrigin.w;
    glm::vec2 ndcRight  = glm::vec2(clipRight) / clipRight.w;
    float ndcLength = glm::length(ndcRight - ndcOrigin);
    if (ndcLength < glm::epsilon<float>()) return 1.0f;

    // Desired size in NDC, scaled with UI/DPI.
    float ndcDesired = EditorStyle::px(GIZMO_SIZE_PIXELS) / (m_vpWidth * 0.5f);
    return ndcDesired / ndcLength;
}

float TransformGizmo::intersectRayPlane(
    const Math::Ray& ray,
    const glm::vec3& planePoint,
    const glm::vec3& planeNormal
) {
    float denom = glm::dot(planeNormal, ray.direction);
    if (std::abs(denom) < glm::epsilon<float>()) return -1.0f;
    return glm::dot(planePoint - ray.origin, planeNormal) / denom;
}

float TransformGizmo::distPointToSegment2D(ImVec2 p, ImVec2 a, ImVec2 b) {
    ImVec2 ab(b.x - a.x, b.y - a.y);
    ImVec2 ap(p.x - a.x, p.y - a.y);
    float abLen2 = ab.x * ab.x + ab.y * ab.y;
    if (abLen2 < glm::epsilon<float>()) return std::sqrt(ap.x * ap.x + ap.y * ap.y);

    float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / abLen2, 0.0f, 1.0f);
    float dx = p.x - (a.x + t * ab.x);
    float dy = p.y - (a.y + t * ab.y);
    return std::sqrt(dx * dx + dy * dy);
}

int TransformGizmo::axisIndex(GizmoElement elem) {
    for (int i = 0; i < 3; ++i) {
        if (GIZMO_AXES[i] == elem) return i;
    }
    return -1;
}

glm::vec3 TransformGizmo::getAxisDirection(GizmoElement elem, const glm::vec3 axes[3]) const {
    const int i = axisIndex(elem);
    return i < 0 ? glm::vec3(0.0f) : axes[i];
}

glm::vec3 TransformGizmo::getDragPlaneNormal(GizmoElement elem, const glm::vec3 axes[3]) const {
    // A plane handle drags in its own plane, so its normal is the axis it faces.
    for (int i = 0; i < 3; ++i) {
        if (GIZMO_PLANES[i] == elem) return axes[i];
    }

    const int i = axisIndex(elem);
    if (i < 0) return m_cameraDir;

    // An axis drags in the plane that contains it and faces the camera. Looking
    // straight down it there is no such plane, so fall back to another axis
    // rather than normalise noise - only non-degeneracy matters here.
    const glm::vec3 cross = glm::cross(m_cameraDir, axes[i]);
    if (glm::length(cross) < MIN_DRAG_PLANE_CROSS) return axes[i == 0 ? 1 : 0];
    return glm::normalize(glm::cross(axes[i], cross));
}

bool TransformGizmo::planeQuadCorners(
    int i,
    const ImVec2 screenAxes[3],
    const bool axisOk[3],
    ImVec2& qA,
    ImVec2& qB,
    ImVec2& qC
) const {
    const int a1 = (i + 1) % 3;
    const int a2 = (i + 2) % 3;
    if (!axisOk[a1] || !axisOk[a2]) return false;

    const ImVec2 dA(
        (screenAxes[a1].x - m_originScreen.x) * PLANE_QUAD_FRAC,
        (screenAxes[a1].y - m_originScreen.y) * PLANE_QUAD_FRAC
    );
    const ImVec2 dB(
        (screenAxes[a2].x - m_originScreen.x) * PLANE_QUAD_FRAC,
        (screenAxes[a2].y - m_originScreen.y) * PLANE_QUAD_FRAC
    );
    qA = ImVec2(m_originScreen.x + dA.x,        m_originScreen.y + dA.y);
    qB = ImVec2(m_originScreen.x + dB.x,        m_originScreen.y + dB.y);
    qC = ImVec2(m_originScreen.x + dA.x + dB.x, m_originScreen.y + dA.y + dB.y);
    return true;
}

ImU32 TransformGizmo::colorForElement(GizmoElement elem, GizmoElement highlight) const {
    if (elem == highlight) return COLOR_HIGHLIGHTED;
    switch (elem) {
        case GizmoElement::AxisX: case GizmoElement::PlaneYZ: return COLOR_X;
        case GizmoElement::AxisY: case GizmoElement::PlaneXZ: return COLOR_Y;
        case GizmoElement::AxisZ: case GizmoElement::PlaneXY: return COLOR_Z;
        default: return IM_COL32(200, 200, 200, 200);
    }
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
    float vpHeight,
    bool pointerFree
) {
    m_viewProj = projection * view;
    m_invViewProj = glm::inverse(m_viewProj);
    m_vpMin = vpMin;
    m_vpWidth = vpWidth;
    m_vpHeight = vpHeight;
    glm::mat4 invView = glm::inverse(view);
    m_cameraDir   = -glm::normalize(glm::vec3(invView[2]));
    m_cameraRight = glm::normalize(glm::vec3(invView[0]));

    m_gizmoOrigin = glm::vec3(model[3]);

    // Don't draw/interact when the entity is behind the near plane
    if (!project(m_gizmoOrigin, m_originScreen)) {
        // Nothing was drawn and nothing was hit-tested, so nothing may be left
        // claiming otherwise - the hover included, and the drag's last angle
        // with it.
        cancelInteraction();
        return false;
    }

    m_screenFactor = computeScreenFactor(m_gizmoOrigin);
    m_mousePos = ImGui::GetMousePos();

    glm::vec3 axes[3];
    if (mode == GizmoMode::World) {
        axes[0] = glm::vec3(1, 0, 0);
        axes[1] = glm::vec3(0, 1, 0);
        axes[2] = glm::vec3(0, 0, 1);
    } else {
        axes[0] = glm::normalize(glm::vec3(model[0]));
        axes[1] = glm::normalize(glm::vec3(model[1]));
        axes[2] = glm::normalize(glm::vec3(model[2]));
    }

    // An axis tip one screen factor out can sit behind the near plane while the
    // origin does not, and a point behind it has no screen position at all - so
    // every handle derived from that tip is dropped rather than placed.
    ImVec2 screenAxes[3];
    bool   axisOk[3];
    for (int i = 0; i < 3; ++i) {
        axisOk[i] = project(m_gizmoOrigin + axes[i] * m_screenFactor, screenAxes[i]);
    }

    bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool modified = false;
    // The early return above settled the origin for this frame; a drag can put
    // the new one behind the near plane.
    bool originOk = true;

    if (m_dragging) {
        if (!mouseDown) {
            endDrag();
        } else {
            switch (operation) {
                case GizmoOperation::Translate: modified = handleTranslationDrag(model, axes); break;
                case GizmoOperation::Rotate:    modified = handleRotationDrag(model, axes);    break;
                case GizmoOperation::Scale:     modified = handleScaleDrag(model, axes);       break;
            }

            if (modified) {
                m_gizmoOrigin = glm::vec3(model[3]);
                originOk = project(m_gizmoOrigin, m_originScreen);
                if (mode == GizmoMode::Local) {
                    axes[0] = glm::normalize(glm::vec3(model[0]));
                    axes[1] = glm::normalize(glm::vec3(model[1]));
                    axes[2] = glm::normalize(glm::vec3(model[2]));
                }
                for (int i = 0; i < 3; ++i) {
                    axisOk[i] = project(m_gizmoOrigin + axes[i] * m_screenFactor, screenAxes[i]);
                }
            }
        }
    } else {
        bool inViewport = m_mousePos.x >= m_vpMin.x && m_mousePos.x <= m_vpMin.x + m_vpWidth
            && m_mousePos.y >= m_vpMin.y && m_mousePos.y <= m_vpMin.y + m_vpHeight;

        if (inViewport && pointerFree) {
            switch (operation) {
                case GizmoOperation::Translate: m_hovered = hitTestTranslation(screenAxes, axisOk); break;
                case GizmoOperation::Rotate:    m_hovered = hitTestRotation(axes);                  break;
                case GizmoOperation::Scale:     m_hovered = hitTestScale(screenAxes, axisOk);       break;
            }
        } else {
            m_hovered = GizmoElement::None;
        }

        if (m_hovered != GizmoElement::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_active = m_hovered;
            m_dragging = true;
            m_dragStartModel = model;
            m_dragPlaneNormal = getDragPlaneNormal(m_active, axes);
            m_dragPlanePoint = m_gizmoOrigin;

            // Once at drag-start. Skew and perspective are discarded, the gizmo
            // driving only translation, rotation and scale.
            {
                glm::vec3 skew;
                glm::vec4 persp;
                glm::decompose(
                    m_dragStartModel,
                    m_dragStartScale,
                    m_dragStartRot,
                    m_dragStartPos,
                    skew,
                    persp
                );
            }

            const Math::Ray ray = screenToRay(m_mousePos);
            float t = intersectRayPlane(ray, m_dragPlanePoint, m_dragPlaneNormal);
            if (t > 0.0f) {
                m_dragStartWorldHit = ray.origin + ray.direction * t;
            } else {
                m_dragStartWorldHit = m_gizmoOrigin;
            }

            if (operation == GizmoOperation::Rotate) {
                m_rotationAxis = getAxisDirection(m_active, axes);
                m_dragPlaneNormal = m_rotationAxis;
                float t2 = intersectRayPlane(ray, m_dragPlanePoint, m_dragPlaneNormal);
                if (t2 > 0.0f) {
                    glm::vec3 hit = ray.origin + ray.direction * t2;
                    m_rotationStartDir = glm::normalize(hit - m_gizmoOrigin);
                } else {
                    m_rotationStartDir = glm::vec3(1, 0, 0);
                }
            }

            if (operation == GizmoOperation::Scale) {
                glm::vec3 axis = getAxisDirection(m_active, axes);
                m_scaleStartDist = glm::dot(m_dragStartWorldHit - m_gizmoOrigin, axis);
                if (std::abs(m_scaleStartDist) < glm::epsilon<float>()) m_scaleStartDist = 1.0f;
            }
        }
    }

    if (originOk) {
        drawList->PushClipRect(
            ImVec2(m_vpMin.x, m_vpMin.y),
            ImVec2(m_vpMin.x + m_vpWidth, m_vpMin.y + m_vpHeight),
            true
        );

        switch (operation) {
            case GizmoOperation::Translate: drawTranslationGizmo(drawList, screenAxes, axisOk); break;
            case GizmoOperation::Rotate:    drawRotationGizmo(drawList, axes);                  break;
            case GizmoOperation::Scale:     drawScaleGizmo(drawList, screenAxes, axisOk);       break;
        }

        drawList->PopClipRect();
    }

    return modified;
}

GizmoElement TransformGizmo::hitTestTranslation(const ImVec2 screenAxes[3], const bool axisOk[3]) const {
    // Test plane quads first (they're smaller targets, higher priority)
    for (int i = 0; i < 3; ++i) {
        ImVec2 qA, qB, qC;
        if (!planeQuadCorners(i, screenAxes, axisOk, qA, qB, qC)) continue;

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

    float bestDist = EditorStyle::px(AXIS_HIT_RADIUS) + 1.0f;
    GizmoElement bestElem = GizmoElement::None;

    for (int i = 0; i < 3; ++i) {
        if (!axisOk[i]) continue;
        float d = distPointToSegment2D(m_mousePos, m_originScreen, screenAxes[i]);
        if (d < EditorStyle::px(AXIS_HIT_RADIUS) && d < bestDist) {
            bestDist = d;
            bestElem = GIZMO_AXES[i];
        }
    }

    return bestElem;
}

GizmoElement TransformGizmo::hitTestRotation(const glm::vec3 axes[3]) const {
    const Math::Ray ray = screenToRay(m_mousePos);
    float ringRadius = m_screenFactor;

    float bestDist = EditorStyle::px(AXIS_HIT_RADIUS) + 1.0f;
    GizmoElement bestElem = GizmoElement::None;

    for (int i = 0; i < 3; ++i) {
        float t = intersectRayPlane(ray, m_gizmoOrigin, axes[i]);
        if (t < 0.0f) continue;

        glm::vec3 hit = ray.origin + ray.direction * t;

        // The same facing test the draw applies, so a click lands on the half of
        // the ring that is shown: the far side is faded to nothing where it turns
        // edge-on, and would otherwise still answer the ray.
        const float faceDot = glm::dot(toViewer(hit), axes[i]);
        if (std::abs(faceDot) <= RING_FACING_MIN) continue;

        float distFromCenter = glm::length(hit - m_gizmoOrigin);

        float diff = std::abs(distFromCenter - ringRadius);
        // Convert world-space diff to screen pixels for threshold. Both samples
        // have to project: a ring behind the near plane would otherwise measure
        // a zero-pixel gap between two dropped points and win every click.
        ImVec2 hitScreen{}, hitOffScreen{};
        if (!project(hit, hitScreen)) continue;
        if (!project(hit + glm::normalize(hit - m_gizmoOrigin) * diff, hitOffScreen)) continue;

        const float dx = hitScreen.x - hitOffScreen.x;
        const float dy = hitScreen.y - hitOffScreen.y;
        float pixelDiff = std::sqrt(dx * dx + dy * dy);

        if (pixelDiff < EditorStyle::px(AXIS_HIT_RADIUS) && pixelDiff < bestDist) {
            bestDist = pixelDiff;
            bestElem = GIZMO_AXES[i];
        }
    }

    return bestElem;
}

GizmoElement TransformGizmo::hitTestScale(const ImVec2 screenAxes[3], const bool axisOk[3]) const {
    // Same as translation axis test (lines) plus box handle at endpoints
    float bestDist = EditorStyle::px(AXIS_HIT_RADIUS) + 1.0f;
    GizmoElement bestElem = GizmoElement::None;

    for (int i = 0; i < 3; ++i) {
        if (!axisOk[i]) continue;
        float dx = m_mousePos.x - screenAxes[i].x;
        float dy = m_mousePos.y - screenAxes[i].y;
        float boxDist = std::max(std::abs(dx), std::abs(dy));
        if (boxDist < EditorStyle::px(SCALE_BOX_HALF + 4.0f) && boxDist < bestDist) {
            bestDist = boxDist;
            bestElem = GIZMO_AXES[i];
            continue;
        }

        float d = distPointToSegment2D(m_mousePos, m_originScreen, screenAxes[i]);
        if (d < EditorStyle::px(AXIS_HIT_RADIUS) && d < bestDist) {
            bestDist = d;
            bestElem = GIZMO_AXES[i];
        }
    }

    return bestElem;
}

bool TransformGizmo::handleTranslationDrag(glm::mat4& model, const glm::vec3 axes[3]) {
    const Math::Ray ray = screenToRay(m_mousePos);
    float t = intersectRayPlane(ray, m_dragPlanePoint, m_dragPlaneNormal);
    if (t < 0.0f) return false;

    glm::vec3 currentHit = ray.origin + ray.direction * t;
    glm::vec3 delta = currentHit - m_dragStartWorldHit;

    // An axis handle drags along its axis; a plane handle's delta is already in
    // its plane, and snaps along the two axes that span it.
    const int axisIdx = axisIndex(m_active);
    if (axisIdx >= 0) {
        const glm::vec3 axis = getAxisDirection(m_active, axes);
        delta = axis * snapTravel(glm::dot(delta, axis), m_snap.distance);
    } else if (m_snap.distance > 0.0f) {
        for (int i = 0; i < 3; ++i) {
            if (GIZMO_PLANES[i] != m_active) continue;
            const glm::vec3& a = axes[(i + 1) % 3];
            const glm::vec3& b = axes[(i + 2) % 3];
            delta = a * snapTravel(glm::dot(delta, a), m_snap.distance)
                + b * snapTravel(glm::dot(delta, b), m_snap.distance);
        }
    }

    model = m_dragStartModel;
    model[3] = m_dragStartModel[3] + glm::vec4(delta, 0.0f);
    return true;
}

bool TransformGizmo::handleRotationDrag(glm::mat4& model, const glm::vec3 axes[3]) {
    const Math::Ray ray = screenToRay(m_mousePos);
    float t = intersectRayPlane(ray, m_dragPlanePoint, m_dragPlaneNormal);
    if (t < 0.0f) return false;

    glm::vec3 currentHit = ray.origin + ray.direction * t;
    glm::vec3 currentDir = currentHit - m_gizmoOrigin;
    float currentLen = glm::length(currentDir);
    if (currentLen < glm::epsilon<float>()) return false;
    currentDir /= currentLen;

    float dotVal = std::clamp(glm::dot(m_rotationStartDir, currentDir), -1.0f, 1.0f);
    float crossDot = glm::dot(glm::cross(m_rotationStartDir, currentDir), m_rotationAxis);
    float angle = std::atan2(crossDot, dotVal);

    // The angle itself, not an Euler decomposition of the result, which would
    // snap through gimbal lock.
    angle = snapTravel(angle, m_snap.angle);

    m_dragRotation = glm::angleAxis(angle, m_rotationAxis);

    // Build rotation matrix around the axis through the gizmo origin
    glm::mat4 toOrigin = glm::translate(glm::mat4(1.0f), -m_gizmoOrigin);
    glm::mat4 fromOrigin = glm::translate(glm::mat4(1.0f), m_gizmoOrigin);
    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), angle, m_rotationAxis);

    model = fromOrigin * rot * toOrigin * m_dragStartModel;
    return true;
}

bool TransformGizmo::handleScaleDrag(glm::mat4& model, const glm::vec3 axes[3]) {
    const Math::Ray ray = screenToRay(m_mousePos);
    float t = intersectRayPlane(ray, m_dragPlanePoint, m_dragPlaneNormal);
    if (t < 0.0f) return false;

    glm::vec3 currentHit = ray.origin + ray.direction * t;
    glm::vec3 axis = getAxisDirection(m_active, axes);
    float currentDist = glm::dot(currentHit - m_gizmoOrigin, axis);

    if (std::abs(m_scaleStartDist) < glm::epsilon<float>()) return false;
    float scaleFactor = currentDist / m_scaleStartDist;
    scaleFactor = std::clamp(scaleFactor, 0.01f, 100.0f);

    // hitTestScale only ever picks an axis, so the refusal below is a statement
    // of that rather than a case.
    const int axisIdx = axisIndex(m_active);
    if (axisIdx < 0) return false;

    // By magnitude: decompose carries a mirror as a negative scale, and the
    // snap is a step in size.
    glm::vec3 newScale = m_dragStartScale;
    const float start = std::abs(newScale[axisIdx]);
    newScale[axisIdx] = std::copysign(snapScale(start, start * scaleFactor, m_snap.scale), newScale[axisIdx]);

    model = glm::translate(glm::mat4(1.0f), m_dragStartPos)
          * glm::mat4_cast(glm::normalize(m_dragStartRot))
          * glm::scale(glm::mat4(1.0f), newScale);
    return true;
}

void TransformGizmo::drawTranslationGizmo(ImDrawList* dl, const ImVec2 screenAxes[3], const bool axisOk[3]) {
    GizmoElement hl = m_dragging ? m_active : m_hovered;

    static constexpr ImU32 PLANE_FILLS[] = { COLOR_PLANE_X, COLOR_PLANE_Y, COLOR_PLANE_Z };
    for (int i = 0; i < 3; ++i) {
        ImVec2 qA, qB, qC;
        if (!planeQuadCorners(i, screenAxes, axisOk, qA, qB, qC)) continue;

        const ImU32 fillColor = (GIZMO_PLANES[i] == hl)
            ? IM_COL32(255, 210, 50, 60)   // highlight, semi-transparent
            : PLANE_FILLS[i];

        dl->AddQuadFilled(m_originScreen, qA, qC, qB, fillColor);
    }

    for (int i = 0; i < 3; ++i) {
        if (!axisOk[i]) continue;
        ImU32 col = colorForElement(GIZMO_AXES[i], hl);
        float thick = (GIZMO_AXES[i] == hl) ? HIGHLIGHT_THICKNESS : LINE_THICKNESS;

        dl->AddLine(m_originScreen, screenAxes[i], col, EditorStyle::px(thick));

        // Arrow head (triangle)
        ImVec2 dir(screenAxes[i].x - m_originScreen.x, screenAxes[i].y - m_originScreen.y);
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 1.0f) {
            dir.x /= len;
            dir.y /= len;
            ImVec2 perp(-dir.y, dir.x);
            float headSize = EditorStyle::px(ARROW_HEAD_PIXELS);
            ImVec2 tip = screenAxes[i];
            ImVec2 base1(
                tip.x - dir.x * headSize * 2.0f + perp.x * headSize,
                tip.y - dir.y * headSize * 2.0f + perp.y * headSize
            );
            ImVec2 base2(
                tip.x - dir.x * headSize * 2.0f - perp.x * headSize,
                tip.y - dir.y * headSize * 2.0f - perp.y * headSize
            );
            dl->AddTriangleFilled(tip, base1, base2, col);
        }
    }

    dl->AddCircleFilled(m_originScreen, EditorStyle::px(CENTRE_DOT_RADIUS), IM_COL32(255, 255, 255, 200), 8);
}

void TransformGizmo::drawRotationGizmo(ImDrawList* dl, const glm::vec3 axes[3]) {
    GizmoElement hl = m_dragging ? m_active : m_hovered;
    float radius = m_screenFactor;

    const float angleStep = 2.0f * glm::pi<float>() / CIRCLE_SEGMENTS;
    const float halfStep  = angleStep * 0.5f;

    for (int i = 0; i < 3; ++i) {
        ImU32 col = colorForElement(GIZMO_AXES[i], hl);
        const float thick = EditorStyle::px((GIZMO_AXES[i] == hl) ? HIGHLIGHT_THICKNESS : LINE_THICKNESS);

        const glm::vec3 normal = axes[i];
        glm::vec3 tangent, bitangent;
        orthoBasis(normal, tangent, bitangent);

        ImVec2 prevPt{};
        bool   prevOk = false;
        for (int s = 0; s <= CIRCLE_SEGMENTS; ++s) {
            float angle = s * angleStep;
            glm::vec3 worldPt = m_gizmoOrigin
                + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
            ImVec2 pt{};
            const bool ok = project(worldPt, pt);

            if (s > 0 && ok && prevOk) {
                // Only draw segments facing the camera (back-face culling for
                // rings); the facing test samples the segment's midpoint.
                const float midAngle = angle - halfStep;
                const glm::vec3 midDir = tangent * std::cos(midAngle) + bitangent * std::sin(midAngle);
                glm::vec3 midWorld = m_gizmoOrigin + midDir * radius;
                float faceDot = glm::dot(toViewer(midWorld), normal);

                if (std::abs(faceDot) > RING_FACING_MIN || GIZMO_AXES[i] == hl) {
                    float alpha = std::abs(faceDot);
                    alpha = std::clamp(alpha * 3.0f, 0.2f, 1.0f);
                    ImU32 segCol = col;
                    if (GIZMO_AXES[i] != hl) {
                        uint8_t a = static_cast<uint8_t>(255.0f * alpha);
                        segCol = (col & 0x00FFFFFF) | (static_cast<ImU32>(a) << 24);
                    }
                    dl->AddLine(prevPt, pt, segCol, thick);
                }
            }
            prevPt = pt;
            prevOk = ok;
        }
    }

    dl->AddCircleFilled(m_originScreen, EditorStyle::px(CENTRE_DOT_RADIUS), IM_COL32(255, 255, 255, 200), 8);
}

void TransformGizmo::drawScaleGizmo(ImDrawList* dl, const ImVec2 screenAxes[3], const bool axisOk[3]) {
    GizmoElement hl = m_dragging ? m_active : m_hovered;

    for (int i = 0; i < 3; ++i) {
        if (!axisOk[i]) continue;
        ImU32 col = colorForElement(GIZMO_AXES[i], hl);
        const float thick = EditorStyle::px((GIZMO_AXES[i] == hl) ? HIGHLIGHT_THICKNESS : LINE_THICKNESS);

        dl->AddLine(m_originScreen, screenAxes[i], col, thick);

        // Box handle at endpoint
        const float half = EditorStyle::px(SCALE_BOX_HALF);
        ImVec2 boxMin(screenAxes[i].x - half, screenAxes[i].y - half);
        ImVec2 boxMax(screenAxes[i].x + half, screenAxes[i].y + half);
        dl->AddRectFilled(boxMin, boxMax, col);
    }
}

} // namespace Vkm::Engine
