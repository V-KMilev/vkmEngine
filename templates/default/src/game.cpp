#define VKM_LOG_CATEGORY "GAME"

#include "game.h"

namespace Game {

void Spinner::onStart() {
    LOG_INFO("Spinner on entity %u turns at %.0f degrees a second", entity().slot(), degreesPerSecond);
}

void Spinner::onUpdate(float dt) {
    Transform* transform = tryGet<Transform>();
    if (!transform || glm::dot(axis, axis) <= glm::epsilon<float>()) return;

    const glm::quat spin = glm::angleAxis(glm::radians(degreesPerSecond * dt), glm::normalize(axis));
    transform->rotation = glm::normalize(transform->rotation * spin);
}

void Spinner::onCollisionEnter(const Collision& hit) {
    LOG_INFO("Spinner hit by entity %u; turning the other way", hit.other.slot());
    degreesPerSecond = -degreesPerSecond;
}

} // namespace Game
