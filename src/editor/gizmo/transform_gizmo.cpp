#include "gizmo/transform_gizmo.h"

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

    // Desired size in NDC, scaled with UI/DPI so the gizmo isn't a tiny
    // overlay on a 4K display.
    float ndcDesired = (GIZMO_SIZE_PIXELS * m_uiScale) / (m_vpWidth * 0.5f);
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
    // Font size 13 is ImGui's documented default. On high-DPI displays the
    // font grows and so should the gizmo's hit/visual scale.
    m_uiScale = std::max(1.0f, ImGui::GetFontSize() / 13.0f);

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
            // Release. m_dragRotation is meaningful only during a rotation
            // drag; clear it unconditionally so non-rotate gizmo modes don't
            // observe a stale quaternion from the previous rotation session.
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

            // Decompose once at drag-start. Skew/perspective are discarded
            // (the gizmo only ever drives translation/rotation/scale), and
            // re-decomposing each frame would just throw the same result
            // away while costing CPU.
            {
                glm::vec3 skew;
                glm::vec4 persp;
                glm::decompose(m_dragStartModel, m_dragStartScale, m_dragStartRot,
                               m_dragStartPos, skew, persp);
            }

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
