#include "camera.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

Camera::Camera(
    uint32_t id,
    ProjectionType projection
) : Component(id, ComponentType::Camera),
    m_projection(projection),
    m_fovY(glm::radians(60.0f)),
    m_aspect(16.0f / 9.0f),
    m_near(0.1f),
    m_far(1000.0f),
    m_orthoHeight(10.0f),
    m_active(true),
    m_exposure(1.0f) {}

} // namespace Engine
