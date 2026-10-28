#include "lab_wind.h"

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "core/math/easing.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/scene.h"

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
    const double now   = worldTime();
    const double every = gustSeconds;
    const double quick = every / FLUTTER_RATE;
    const float  ahead = phase * FLUTTER_RATE;
    const float  tau   = glm::two_pi<float>();
    const float  gust  = LULL + (1.0f - LULL) * (0.5f - 0.5f * std::cos(tau * (cycle(now, every) + phase)));
    const float  low   = std::sin(tau * (cycle(now, quick) + ahead));
    const float  high  = std::sin(tau * (cycle(now, quick / FLUTTER_RATIO) + ahead * FLUTTER_RATIO));
    const float  shake = 0.5f * (low + high);

    body->linearVelocity += direction * (gust * (1.0f + flutter * shake) * dt);
    Rigidbody::wake(*body);
}

} // namespace Lab
