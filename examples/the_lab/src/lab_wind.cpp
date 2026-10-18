#include "lab_wind.h"

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "ecs/component/physics/rigidbody.h"
#include "ecs/scene.h"
#include "net/net_session.h"

namespace Lab {

namespace {

// How much of the wind blows between gusts, as a share of a gust.
constexpr float LULL = 0.35f;

// How many times quicker the flutter shakes than the gusts come, and its two notes' ratio.
constexpr float FLUTTER_RATE  = 7.0f;
constexpr float FLUTTER_RATIO = 1.618f;

} // namespace

void LabWind::onFixedUpdate(float dt) {
    if (!isSimulated()) return;
    Rigidbody* body = tryGet<Rigidbody>();
    if (!body || body->motion != RigidbodyMotion::Dynamic || gustSeconds <= 0.0f) return;

    // Inside a tick the world's clock is that tick's, the same at every end.
    const double now   = net().worldTime(clock());
    const double gusts = now / static_cast<double>(gustSeconds) + static_cast<double>(phase);
    const double fast  = gusts * static_cast<double>(FLUTTER_RATE);
    const auto   turn  = [](double turns) { return static_cast<float>(glm::fract(turns)); };
    const float  tau   = glm::two_pi<float>();
    const float  gust  = LULL + (1.0f - LULL) * (0.5f - 0.5f * std::cos(tau * turn(gusts)));
    const float  low   = std::sin(tau * turn(fast));
    const float  high  = std::sin(tau * turn(fast * static_cast<double>(FLUTTER_RATIO)));
    const float  shake = 0.5f * (low + high);

    body->linearVelocity += direction * (gust * (1.0f + flutter * shake) * dt);
    Rigidbody::wake(*body);
}

} // namespace Lab
