#include "game.h"

#include <glm/gtc/quaternion.hpp>

#include "ecs/scene.h"
#include "ecs/component/transform.h"

namespace Game {

void Spinner::onUpdate(float dt) {
    Engine::Scene& scene = *context().scene;
    if (!scene.has<Engine::Transform>(m_entity)) return;

    Engine::Transform& transform = scene.get<Engine::Transform>(m_entity);
    transform.rotation = glm::normalize(transform.rotation *
        glm::angleAxis(glm::radians(degreesPerSecond * dt), glm::vec3(0.0f, 1.0f, 0.0f)));
}

} // namespace Game
