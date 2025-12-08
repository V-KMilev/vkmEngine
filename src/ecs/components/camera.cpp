#include "camera.h"

#include "cpu_camera.h"

namespace Engine {

Camera::Camera(
    uint32_t id
) : Component(id, ComponentType::Camera),
    m_camera(nullptr) {}

void Camera::setCamera(std::shared_ptr<CPUCamera> && camera) {
    m_camera = std::move(camera);
}

} // namespace Engine
