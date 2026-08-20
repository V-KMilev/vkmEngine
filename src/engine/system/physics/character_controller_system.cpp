#define VKM_LOG_CATEGORY "PHYSICS"

#include "system/physics/character_controller_system.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "logger.h"

#include "core/clock.h"
#include "debug/profiler.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/scene.h"

namespace Vkm::Engine {

namespace {

// The engine's world up, and the axis a capsule collider stands along.
const glm::vec3 UP = {0.0f, 1.0f, 0.0f};

// Below this the input is nothing, not a very slow walk. Guards the normalize
// in the acceleration step and keeps a dead stick from waking a sleeping body.
constexpr float MOVE_EPS = 1e-4f;

bool hasCapsule(const Collider& collider) {
    for (const ColliderPart& part : collider.parts)
        if (part.shape == ColliderShape::Capsule) return true;
    return false;
}

} // namespace

void CharacterControllerSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("CharacterControllerSystem");

    Scene& scene = ctx.scene;
    const float dt = ctx.clock.getFixedStep();

    bool sawNoCapsule = false;
    bool sawSpinnable = false;

    scene.forEach<CharacterController, Rigidbody>(
            [&](EntityId id, CharacterController& cc, Rigidbody& rb) {
        const Collider* collider = scene.has<Collider>(id) ? &scene.get<Collider>(id) : nullptr;
        if (!collider || !collider->enabled || !hasCapsule(*collider)) sawNoCapsule = true;
        if (!rb.freezeRotation) sawSpinnable = true;

        // Grounded is a question about the surface, not about touching: a wall
        // is a resolved contact too, and standing on one is not standing.
        const float slopeLimit = std::cos(glm::radians(glm::clamp(cc.maxSlopeAngle, 0.0f, 90.0f)));
        cc.grounded = rb.supported && glm::dot(rb.supportNormal, UP) >= slopeLimit;
        cc.groundNormal = cc.grounded ? rb.supportNormal : UP;

        // A body that dozed off has its velocity zeroed by the solver's
        // writeback, so anything asking it to move has to wake it first.
        const bool wants = glm::dot(cc.moveInput, cc.moveInput) > MOVE_EPS || cc.jumpRequested;
        if (wants && rb.sleeping) {
            rb.sleeping = false;
            rb.sleepTimer = 0.0f;
        }

        // Grounded, the target follows the ground plane, so walking up a ramp
        // neither launches off the top nor is dragged back by gravity fighting
        // the contact. Airborne, only the horizontal is steered and the vertical
        // is gravity's alone.
        glm::vec3 velocity = rb.linearVelocity;
        glm::vec3 target = cc.moveInput;
        if (cc.grounded) {
            target -= cc.groundNormal * glm::dot(target, cc.groundNormal);
            // Already rising faster than the ground would carry it: it has just
            // jumped, or been thrown, and that climb is not the controller's to
            // undo. Grounding lags the contacts by a tick, so without this the
            // first ticks of every jump are steered back into the floor.
            target.y = glm::max(target.y, velocity.y);
        } else {
            target.y = velocity.y;
        }

        // A capped step toward the target rather than an exponential approach:
        // acceleration then means m/s^2 at every speed, which is what an author
        // setting the number expects, and it is frame-rate independent.
        const float rate = cc.acceleration * (cc.grounded ? 1.0f : glm::max(cc.airControl, 0.0f));
        const glm::vec3 delta = target - velocity;
        const float distance = glm::length(delta);
        const float step = rate * dt;
        velocity += distance > step && distance > MOVE_EPS ? delta * (step / distance) : delta;

        // Consumed whether or not it could be honoured: a jump held down would
        // otherwise fire the instant the character next touched anything.
        if (cc.jumpRequested) {
            if (cc.grounded) {
                velocity.y = cc.jumpSpeed;
                // Reported immediately, not next tick: the contact that was
                // holding it up survives a tick or two of separation, and a
                // player mashing the key would spend it on a second jump.
                cc.grounded = false;
            }
            cc.jumpRequested = false;
        }

        rb.linearVelocity = velocity;
    });

    if (sawNoCapsule && !m_noCapsuleLogged)
        LOG_WARNING("CharacterController: an entity has no enabled capsule Collider - "
                    "it will never report grounded");
    if (sawSpinnable && !m_spinnableLogged)
        LOG_WARNING("CharacterController: an entity's Rigidbody has freezeRotation off - "
                    "contacts will topple it");
    m_noCapsuleLogged = sawNoCapsule;
    m_spinnableLogged = sawSpinnable;
}

} // namespace Vkm::Engine
