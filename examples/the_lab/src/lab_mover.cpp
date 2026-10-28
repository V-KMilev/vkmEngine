#include "lab_mover.h"

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "core/math/axes.h"
#include "core/math/easing.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/scene.h"

namespace Lab {

namespace {

constexpr double DEGREES_A_TURN = 360.0;

} // namespace

void LabMover::onStart() {
    if (const Transform* at = tryGet<Transform>()) {
        m_origin = at->position;
        m_facing = at->rotation;
    }
}

void LabMover::onFixedUpdate(float) {
    if (!isSimulated()) return;
    Transform* at = tryGet<Transform>();
    if (!at) return;

    // Inside a tick the world's clock is that tick's, the same at every end.
    const double now = worldTime();
    const float  tau = glm::two_pi<float>();
    glm::vec3 velocity{0.0f};
    if (period > 0.0f) {
        const float angle = tau * (cycle(now, period) + phase);
        at->position = m_origin + travel * (0.5f - 0.5f * std::cos(angle));
        velocity     = travel * (0.5f * std::sin(angle) * tau / period);
    }
    const float turn = tau * cycle(now * static_cast<double>(spin), DEGREES_A_TURN);
    at->rotation = glm::angleAxis(turn, Math::WORLD_UP) * m_facing;

    if (Rigidbody* body = tryGet<Rigidbody>()) {
        body->linearVelocity  = velocity;
        body->angularVelocity = Math::WORLD_UP * glm::radians(spin);
    }
}

} // namespace Lab
