#include "transform.h"

#include "cpu_transform.h"

namespace Engine {

Transform::Transform(
    uint32_t id
) : Component(id, ComponentType::Transform),
    m_transform(nullptr) {}

void Transform::setTransform(std::shared_ptr<CPUTransform> && transform) {
    m_transform = std::move(transform);
}

} // namespace Engine