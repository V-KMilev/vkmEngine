#include "lab_orbit.h"

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "base/math/axes.h"
#include "base/math/cycle.h"
#include "base/math/rotation.h"
#include "ecs/component/scene/transform.h"
#include "ecs/scene.h"

namespace Lab {

void LabOrbit::onStart() {
    if (const Transform* at = tryGet<Transform>()) {
        m_center = at->position;
        m_facing = at->rotation;
    }
}

void LabOrbit::onUpdate(float) {
    Transform* at = tryGet<Transform>();
    if (!at) return;
    const double now   = worldTime();
    const float  tau   = glm::two_pi<float>();
    const float  angle = tau * (Math::cycle(now, period) + phase);
    const float  rise  = bob * std::sin(tau * Math::cycle(now, bobSeconds));

    const glm::vec3 out = {std::cos(angle), 0.0f, std::sin(angle)};
    at->position = m_center + out * radius + Math::WORLD_UP * rise;

    // Along the circle, the way round it goes; leaning in toward the center.
    const float way = period < 0.0f ? -1.0f : 1.0f;
    const glm::vec3 travel = glm::vec3(-out.z, 0.0f, out.x) * way;
    const glm::quat lean = glm::angleAxis(glm::radians(bank) * way, Math::WORLD_FORWARD);
    at->rotation = Math::lookRotation(travel) * lean * m_facing;
}

} // namespace Lab
