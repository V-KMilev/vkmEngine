#include "lab_mover.h"

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "core/math/axes.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/scene.h"
#include "net/net_session.h"

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

    // Inside a tick the world's clock is that tick's, the same at every end. Whole turns are dropped,
    // in double, before each angle is taken, so it stays exact however long the lab runs.
    const double now   = net().worldTime(clock());
    const float  tau   = glm::two_pi<float>();
    glm::vec3 velocity{0.0f};
    if (period > 0.0f) {
        const double turns = now / static_cast<double>(period) + static_cast<double>(phase);
        const float  angle = tau * static_cast<float>(glm::fract(turns));
        at->position = m_origin + travel * (0.5f - 0.5f * std::cos(angle));
        velocity     = travel * (0.5f * std::sin(angle) * tau / period);
    }
    const double spun = now * static_cast<double>(spin) / DEGREES_A_TURN;
    const float  turn = tau * static_cast<float>(glm::fract(spun));
    at->rotation = glm::angleAxis(turn, Math::WORLD_UP) * m_facing;

    if (Rigidbody* body = tryGet<Rigidbody>()) {
        body->linearVelocity  = velocity;
        body->angularVelocity = Math::WORLD_UP * glm::radians(spin);
    }
}

} // namespace Lab
