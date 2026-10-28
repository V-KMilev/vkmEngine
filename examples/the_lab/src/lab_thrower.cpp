#include "lab_thrower.h"

#include <glm/glm.hpp>

#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "platform/input/input_map.h"
#include "platform/window/glfw_include.h"

#include "lab_actions.h"
#include "lab_seat.h"

namespace Lab {

namespace {

// Where a crate leaves from, ahead of and above the character's feet, metres.
constexpr float MUZZLE_FORWARD = 1.0f;
constexpr float MUZZLE_HEIGHT  = 1.2f;

// The crate's lift, as a share of the throw's speed.
constexpr float LOFT = 0.25f;

} // namespace

void LabThrower::onStart() {
    input().define(ACTION_THROW, { InputBinding::key(GLFW_KEY_G) });
}

void LabThrower::onFixedUpdate(float dt) {
    // The server builds what every end must see; offline only the player's own seat throws.
    if (!isAuthority() || sitsOutOffline(scene(), net(), entity())) return;
    m_wait = glm::max(0.0f, m_wait - dt);
    if (m_wait > 0.0f || !input().pressed(command(), ACTION_THROW)) return;
    const Ragdoll* ragdoll = tryGet<Ragdoll>();
    const Transform* from = tryGet<Transform>();
    if (!from || (ragdoll && ragdoll->active)) return;

    const glm::vec3 aim = Math::computeForward(from->rotation);
    Transform at;
    at.position = from->position + aim * MUZZLE_FORWARD + Math::WORLD_UP * MUZZLE_HEIGHT;
    at.rotation = from->rotation;
    const EntityId thrown = instantiateShared(crate, at);
    if (Rigidbody* body = scene().tryGet<Rigidbody>(thrown)) {
        body->linearVelocity = aim * speed + Math::WORLD_UP * (speed * LOFT);
    }
    m_wait = cooldown;
}

} // namespace Lab
