#include "gizmo/transform_gizmo.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Engine {

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
    (void)axes;
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

} // namespace Engine
