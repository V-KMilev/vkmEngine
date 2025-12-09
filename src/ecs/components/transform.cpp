#include "transform.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine {

/**
 * @brief Forward direction vector.
 * @brief Right direction vector.
 * @brief Up direction vector.
 */
constexpr glm::vec3 forward = {0.0f, 0.0f, 1.0f};
constexpr glm::vec3 right   = {1.0f, 0.0f, 0.0f};
constexpr glm::vec3 up      = {0.0f, 1.0f, 0.0f};

Transform::Transform(
    uint32_t id,
    const glm::vec3& position,
    const glm::quat& rotation,
    const glm::vec3& scale
) : Component(id, ComponentType::Transform),
    m_position(position),
    m_rotation(glm::normalize(rotation)),
    m_scale(scale),
    m_modelMatrix(1.0f),
    m_dirty(true) {}

void Transform::setPosition(const glm::vec3& position) {
    m_position = position;
    m_dirty = true;
}

void Transform::setRotation(const glm::quat& rotation) {
    m_rotation = glm::normalize(rotation);
    m_dirty = true;
}

void Transform::setScale(const glm::vec3& scale) {
    m_scale = scale;
    m_dirty = true;
}

glm::vec3 Transform::getForward() const {
    return glm::normalize(m_rotation * forward);
}

glm::vec3 Transform::getRight() const {
    return glm::normalize(m_rotation * glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::vec3 Transform::getUp() const {
    return glm::normalize(m_rotation * glm::vec3(0.0f, 1.0f, 0.0f));
}

const glm::mat4& Transform::getModelMatrix() const {
    if (m_dirty) {
        updateModelMatrix();
    }
    return m_modelMatrix;
}


void Transform::updateModelMatrix() const {
    m_modelMatrix  = glm::mat4(1.0f);

    m_modelMatrix  = glm::translate(m_modelMatrix, m_position);
    m_modelMatrix *= glm::mat4_cast(m_rotation);
    m_modelMatrix  = glm::scale(m_modelMatrix, m_scale);

    m_dirty = false;
}

} // namespace Engine