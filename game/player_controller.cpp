#include "player_controller.h"

#include <glm/glm.hpp>

#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "platform/window/input_handle.h"
#include "platform/window/glfw_include.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine {

void PlayerController::onUpdate(float dt) {
    Scene& scene = *context().scene;
    if (!scene.has<Transform>(m_entity)) return;

    const KeyboardInputHandle& keys = context().window->getInputHandle().getKeyboard();
    glm::vec3 move(0.0f);
    if (keys.isKeyPressed(GLFW_KEY_W)) move.z -= 1.0f;  // forward
    if (keys.isKeyPressed(GLFW_KEY_S)) move.z += 1.0f;
    if (keys.isKeyPressed(GLFW_KEY_A)) move.x -= 1.0f;
    if (keys.isKeyPressed(GLFW_KEY_D)) move.x += 1.0f;
    if (move.x == 0.0f && move.z == 0.0f) return;

    Transform& transform = scene.get<Transform>(m_entity);
    transform.position += glm::normalize(move) * (speed * dt);
    HierarchyOperations::markDirty(scene, m_entity);
}

} // namespace Engine
