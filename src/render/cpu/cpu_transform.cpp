#include "cpu_transform.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

CPUTransform::CPUTransform(
    const TransformProperties& properties
) : m_properties(properties) {
    updateModelMatrix();
}

void CPUTransform::setPosition(const glm::vec3& position) {
    m_properties.position = position;
    updateModelMatrix();
}

void CPUTransform::setRotation(const glm::vec3& rotation) {
    m_properties.rotation = rotation;
    updateModelMatrix();
}

void CPUTransform::setScale(const glm::vec3& scale) {
    m_properties.scale = scale;
    updateModelMatrix();
}

void CPUTransform::translate(const glm::vec3& translation) {
    m_properties.position += translation;
    updateModelMatrix();
}

void CPUTransform::rotate(const glm::vec3& rotation) {
    m_properties.rotation += rotation;
    updateModelMatrix();
}

void CPUTransform::scale(const glm::vec3& scale) {
    m_properties.scale *= scale;
    updateModelMatrix();
}

glm::vec3 CPUTransform::getForward() const {
    // Forward vector in local space (negative Z in OpenGL)
    glm::vec3 forward;
    forward.x = std::sin(m_properties.rotation.y) * std::cos(m_properties.rotation.x);
    forward.y = -std::sin(m_properties.rotation.x);
    forward.z = std::cos(m_properties.rotation.y) * std::cos(m_properties.rotation.x);
    return glm::normalize(forward);
}

glm::vec3 CPUTransform::getRight() const {
    // Right vector (cross product of forward and world up)
    return glm::normalize(glm::cross(getForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 CPUTransform::getUp() const {
    // Up vector (cross product of right and forward)
    return glm::normalize(glm::cross(getRight(), getForward()));
}

void CPUTransform::updateModelMatrix() const {
    // Apply transformations in correct order: Scale -> Rotate -> Translate
    // (Matrix multiplication is right-to-left)
    m_modelMatrix = glm::mat4(1.0f);

    // Translation
    m_modelMatrix = glm::translate(m_modelMatrix, m_properties.position);

    // Rotation (Euler angles)
    m_modelMatrix = glm::rotate(m_modelMatrix, m_properties.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch
    m_modelMatrix = glm::rotate(m_modelMatrix, m_properties.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f)); // Yaw
    m_modelMatrix = glm::rotate(m_modelMatrix, m_properties.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f)); // Roll

    // Scale
    m_modelMatrix = glm::scale(m_modelMatrix, m_properties.scale);
}

}