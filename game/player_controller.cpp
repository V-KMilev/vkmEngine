#include "player_controller.h"

#include <glm/glm.hpp>

#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "platform/input/default_bindings.h"
#include "platform/input/input_map.h"
#include "platform/window/glfw_include.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine {

void PlayerController::onUpdate(float dt) {
    Scene& scene = *context().scene;
    if (!scene.has<Transform>(m_entity)) return;

    // Reuses the engine's camera movement actions rather than defining its own:
    // this is a demo controller, and sharing them means rebinding movement once
    // rebinds it here too.
    const InputMap& input = *context().input;
    const glm::vec3 move(input.axis(InputActions::MOVE_RIGHT), 0.0f,
                         -input.axis(InputActions::MOVE_FORWARD));
    if (move.x == 0.0f && move.z == 0.0f) return;

    Transform& transform = scene.get<Transform>(m_entity);
    transform.position += glm::normalize(move) * (speed * dt);
    HierarchyOperations::markDirty(scene, m_entity);
}

} // namespace Engine
