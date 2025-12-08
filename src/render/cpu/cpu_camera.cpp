#include "cpu_camera.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

CPUCamera::CPUCamera(
) : m_properties(),
    m_view(1.0f),
    m_projection(1.0f),
    m_viewProjection(1.0f) {
    updateView();
    updateProjection();
}

CPUCamera::CPUCamera(
    const CameraProperties& properties
) : m_properties(properties),
    m_view(1.0f),
    m_projection(1.0f),
    m_viewProjection(1.0f) {
    updateView();
    updateProjection();
}

void CPUCamera::setPosition(const glm::vec3& position) {
    m_properties.position = position;
    updateView();
}

void CPUCamera::setTarget(const glm::vec3& target) {
    m_properties.target = target;
    updateView();
}

void CPUCamera::setUp(const glm::vec3& up) {
    m_properties.up = up;
    updateView();
}

void CPUCamera::setPerspective(float fovYDegrees, float aspect, float nearPlane, float farPlane) {
    m_properties.fovYDegrees = fovYDegrees;
    m_properties.aspect      = aspect;
    m_properties.nearPlane   = nearPlane;
    m_properties.farPlane    = farPlane;
    updateProjection();
}

void CPUCamera::setAspect(float aspect) {
    m_properties.aspect = aspect;
    updateProjection();
}

void CPUCamera::updateView() {
    m_view = glm::lookAt(m_properties.position, m_properties.target, m_properties.up);
    m_viewProjection = m_projection * m_view;
}

void CPUCamera::updateProjection() {
    m_projection = glm::perspective(
        glm::radians(m_properties.fovYDegrees),
        m_properties.aspect,
        m_properties.nearPlane,
        m_properties.farPlane
    );
    m_viewProjection = m_projection * m_view;
}

} // namespace Engine

