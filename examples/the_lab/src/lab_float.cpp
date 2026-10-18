#include "lab_float.h"

#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/ocean.h"
#include "ecs/physics_settings.h"
#include "ecs/scene.h"
#include "system/ocean/ocean_data.h"

namespace Lab {

namespace {

// An axis this much longer than the thinnest still counts as thin, so a near-cube rolls to any face.
constexpr float FLAT_SLACK = 1.1f;

// The least the box is taken to reach below its middle, metres, so a flat one's share under stays finite.
constexpr float MIN_REACH = 0.01f;

} // namespace

void LabFloat::onStart() {
    m_ocean = findOcean(scene());
}

void LabFloat::onFixedUpdate(float dt) {
    if (!isSimulated()) return;
    const OceanData* sea  = ocean();
    Transform*       at   = tryGet<Transform>();
    Rigidbody*       body = tryGet<Rigidbody>();
    const Transform* rest = scene().tryGet<Transform>(m_ocean);
    if (!sea || !sea->present || !at || !body || !rest || body->motion != RigidbodyMotion::Dynamic) return;

    // How far the box reaches below its middle, however it lies; and which way it floats: on a face
    // of its thinnest axis - a raft lies flat - and of those, the one nearest up.
    const glm::mat3 axes  = glm::mat3_cast(at->rotation);
    const float     thin  = glm::min(halfExtents.x, glm::min(halfExtents.y, halfExtents.z));
    float           reach = 0.0f;
    int             upmost = -1;
    for (int i = 0; i < 3; ++i) {
        reach += glm::abs(axes[i].y) * halfExtents[i];
        const bool flat = halfExtents[i] <= thin * FLAT_SLACK;
        if (flat && (upmost < 0 || glm::abs(axes[i].y) > glm::abs(axes[upmost].y))) upmost = i;
    }
    reach = glm::max(reach, MIN_REACH);

    const OceanSample water = sea->surface.sampleAt({at->position.x, at->position.z});
    const float       depth = rest->position.y + water.height - (at->position.y - reach);
    const float       under = glm::clamp(depth / (2.0f * reach), 0.0f, 1.0f);
    if (under <= 0.0f) return;

    // Lifted at its middle, so it never tips over itself; turned so its face nearest up follows the swell.
    const float     gravity = glm::length(scene().physics().gravity);
    const glm::vec3 face    = axes[upmost] * glm::sign(axes[upmost].y);
    const glm::vec3 turn    = glm::cross(face, water.normal) * (righting * under);

    const float keep = std::exp(-waterDrag * under * dt);
    const glm::vec3 lift = Math::WORLD_UP * (gravity * buoyancy * under * dt);
    body->linearVelocity  = (body->linearVelocity + lift) * keep;
    body->angularVelocity = (body->angularVelocity + turn * dt) * keep;
    Rigidbody::wake(*body);
}

} // namespace Lab
