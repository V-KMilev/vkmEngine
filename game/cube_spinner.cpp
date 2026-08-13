#include "cube_spinner.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine {

void CubeSpinner::onUpdate(float dt) {
    Scene& scene = *context().scene;
    if (!scene.has<Transform>(m_entity)) return;

    Transform& transform = scene.get<Transform>(m_entity);
    const float radians = glm::radians(degreesPerSecond) * dt;
    const glm::quat spin = glm::angleAxis(radians, Math::WORLD_AXIS_Y);
    transform.rotation = glm::normalize(spin * transform.rotation);

    // Visibility recomputes a non-hierarchical entity's model from its local
    // Transform each frame, but a parented entity reads its WorldTransform -
    // mark dirty so HierarchySystem rebuilds it (a no-op without Hierarchy).
    HierarchyOperations::markDirty(scene, m_entity);
}

} // namespace Engine
