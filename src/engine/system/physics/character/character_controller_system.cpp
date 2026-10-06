#define VKM_LOG_CATEGORY "PHYSICS"

#include "system/physics/character/character_controller_system.h"

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
#include "ecs/component/core/transform.h"
#include "net/net_session.h"
#include "core/math/axes.h"
#include "ecs/scene.h"
#include "system/physics/body_pose.h"
#include "system/physics/query/query.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Squared speed, (m/s)^2, below which moveInput asks for no movement.
 *
 * A centimetre a second, so a stick a hair off centre does not wake a body or start a step-up.
 */
constexpr float MOVE_SPEED_SQ = 1e-4f;

/**
 * @brief Turn @p target to run along the surface the body is pressed against instead of into it.
 *
 * Only a surface steeper than the slope limit deflects, taken flat: turning upward would make it a ramp
 * the limit refuses. PhysicsSystem::writeback publishes world up for no contact, which is walkable, so
 * no contact test is needed.
 *
 * @param target Desired velocity, world space.
 * @param blockNormal Rigidbody::blockNormal.
 * @param slopeLimit cos(maxSlopeAngle): at or above it the surface is walkable.
 * @return The target with any component pushing into that surface removed.
 */
glm::vec3 deflectAlongBlock(const glm::vec3& target, const glm::vec3& blockNormal, float slopeLimit) {
    if (glm::dot(blockNormal, Math::WORLD_UP) >= slopeLimit) return target;

    const glm::vec3 flat = {blockNormal.x, 0.0f, blockNormal.z};
    const float lengthSq = glm::dot(flat, flat);
    // A ceiling is unwalkable and has no sideways direction to slide along.
    if (lengthSq <= Physics::DEGENERATE_SQ) return target;

    const glm::vec3 axis = flat / std::sqrt(lengthSq);
    return target - axis * glm::min(0.0f, glm::dot(target, axis));
}

/**
 * @brief The first capsule part of a collider, which is what a character stands on.
 *
 * @param collider Collider to search.
 * @param[out] radius     Its sweep radius.
 * @param[out] halfHeight Half its segment, caps excluded.
 * @param[out] center     Its offset from the entity origin, body-local.
 * @return False when the collider has no capsule, which the caller warns about.
 */
bool capsuleOf(const Collider& collider, float& radius, float& halfHeight, glm::vec3& center) {
    for (const ColliderPart& part : collider.parts) {
        if (part.shape != ColliderShape::Capsule) continue;
        radius = part.radius;
        halfHeight = part.halfHeight;
        center = part.center;
        return true;
    }
    return false;
}

/**
 * @brief Whether what is blocking the character can be mounted, and by how much.
 *
 * Two sweeps, cheap rejection first: forward at step height for room (not under a shelf), then down
 * beyond it for a landing it could have stood on. The probes are a shade narrower than the capsule, so
 * the obstacle already touching its sides does not report itself as in the way.
 *
 * @param scene      Scene to query.
 * @param self       The character, excluded from its own probes.
 * @param feet       World position of the bottom of the capsule.
 * @param radius     Capsule radius.
 * @param direction  Horizontal unit direction of travel.
 * @param stepHeight Tallest rise that may be mounted.
 * @param slopeLimit cos(maxSlopeAngle): at or above it the landing is walkable.
 * @param layerMask  Layers the probes may hit: the body's own collision mask.
 * @param[out] rise  How far up the landing is, when there is one.
 * @return True when a step exists, is clear, and is walkable.
 */
bool findStep(
    Scene& scene,
    EntityId self,
    const glm::vec3& feet,
    float radius,
    const glm::vec3& direction,
    float stepHeight,
    float slopeLimit,
    int layerMask,
    float& rise
) {
    QueryFilter filter;
    filter.ignore = self;
    // Only what the body collides with: a probe hitting its own ragdoll shin blocks every staircase.
    filter.layerMask = layerMask;

    const float probeRadius = radius * 0.9f;
    const float reach = radius * 2.0f;

    // Centre of a sphere just above the step, at the height the character must reach.
    const glm::vec3 mouth = feet + Math::WORLD_UP * (stepHeight + radius);

    RayHit hit;
    if (spherecast(scene, mouth, probeRadius, direction, reach, hit, filter)) {
        return false;
    }

    const glm::vec3 above = mouth + direction * reach;
    const float drop = stepHeight + radius;
    if (!spherecast(scene, above, probeRadius, -Math::WORLD_UP, drop, hit, filter)) {
        return false;
    }
    if (glm::dot(hit.normal, Math::WORLD_UP) < slopeLimit) return false;

    rise = hit.point.y - feet.y;
    return rise > Physics::CONTACT_TOLERANCE && rise <= stepHeight;
}

// Seconds to mount a step, independent of walking speed.
constexpr float STEP_CLIMB_TIME = 0.12f;

// Seconds before a climb is abandoned: past any progressing climb, short of hanging on what cannot be
// mounted.
constexpr float STEP_CLIMB_LIMIT = STEP_CLIMB_TIME * 3.0f;

} // namespace

void CharacterControllerSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("CharacterControllerSystem");

    Scene& scene = ctx.scene;
    const float dt = ctx.clock.getFixedStep();

    bool sawNoCapsule = false;
    bool sawSpinnable = false;

    scene.forEach<CharacterController, Rigidbody>([&](EntityId id, CharacterController& cc, Rigidbody& rb) {
        // Left where the authority put it, grounded included (see netEncode for CharacterController).
        if (!ctx.net.simulates(id)) return;

        // A wall is a resolved contact too, and standing on one is not standing.
        const float slopeLimit = std::cos(glm::radians(glm::clamp(cc.maxSlopeAngle, 0.0f, 90.0f)));
        cc.grounded = rb.supported && glm::dot(rb.supportNormal, Math::WORLD_UP) >= slopeLimit;
        cc.groundNormal = cc.grounded ? rb.supportNormal : Math::WORLD_UP;

        float     radius     = 0.0f;
        float     halfHeight = 0.0f;
        glm::vec3 offset(0.0f);
        const Collider* collider = scene.tryGet<Collider>(id);
        const bool standsOnCapsule = collider && collider->enabled
            && capsuleOf(*collider, radius, halfHeight, offset);

        if (!standsOnCapsule) sawNoCapsule = true;
        if (!rb.freezeRotation) sawSpinnable = true;

        // PhysicsSystem::writeback zeroes a sleeping body's velocity, so a request to move wakes it.
        const float asked = glm::dot(cc.moveInput, cc.moveInput);
        const bool wants = asked > MOVE_SPEED_SQ || cc.jumpRequested;
        if (wants && rb.sleeping) Rigidbody::wake(rb);

        // Deflected before the ground projection, so the slope's vertical follows the final direction.
        const glm::vec3 flatInput = {cc.moveInput.x, 0.0f, cc.moveInput.z};
        const float inputLenSq = glm::dot(flatInput, flatInput);
        const bool blocked = glm::dot(rb.blockNormal, Math::WORLD_UP) < slopeLimit;

        const Transform* local = scene.tryGet<Transform>(id);
        const bool climbable = standsOnCapsule && local && radius > 0.0f;

        // World space, as stepTargetY and the probes are; a parented character's Transform is not.
        glm::vec3 feet(0.0f);
        if (climbable) {
            const BodyPose pose = worldPoseOf(scene, id, *local);
            feet = pose.position + pose.rotation * offset
                - Math::WORLD_UP * (halfHeight + radius);
        }

        // Never ended by the step no longer blocking: rising is what un-blocks a riser.
        const bool wasStepping = cc.stepping;
        if (cc.stepping) {
            cc.stepTime += dt;
            const float reached = cc.stepTargetY - Physics::CONTACT_TOLERANCE;
            const bool arrived = climbable && feet.y >= reached;
            const bool asking = inputLenSq > MOVE_SPEED_SQ;
            // A resting contact settles a slop short, so "arrived" may never be met; the clock ends it.
            const bool expired = cc.stepTime >= STEP_CLIMB_LIMIT;
            if (!climbable || !asking || arrived || expired) cc.stepping = false;
        }

        // Two sweeps, only for a character pushing into something.
        if (!cc.stepping && climbable && cc.grounded && blocked
            && cc.stepHeight > 0.0f && inputLenSq > MOVE_SPEED_SQ) {
            const glm::vec3 forward = flatInput / std::sqrt(inputLenSq);
            float rise = 0.0f;
            const bool mountable = findStep(
                scene,
                id,
                feet,
                radius,
                forward,
                cc.stepHeight,
                slopeLimit,
                rb.collidesWith,
                rise
            );
            if (mountable) {
                cc.stepping = true;
                cc.stepTime = 0.0f;
                cc.stepTargetY = feet.y + rise;
            }
        }

        // Driven by the remaining distance, so a kerb rises by a kerb; capped so the first tick does not
        // teleport.
        const float remaining = climbable ? cc.stepTargetY - feet.y : 0.0f;
        const float stepClimb = (cc.stepping && dt > 0.0f && remaining > 0.0f)
            ? glm::min(remaining / dt, cc.stepHeight / STEP_CLIMB_TIME)
            : 0.0f;

        glm::vec3 velocity = rb.linearVelocity;
        glm::vec3 target = cc.stepping
            ? cc.moveInput
            : deflectAlongBlock(cc.moveInput, rb.blockNormal, slopeLimit);

        // Grounded, the target follows the ground plane, so a ramp neither launches nor drags back.
        // Airborne, the vertical is gravity's alone.
        if (cc.grounded) {
            target -= cc.groundNormal * glm::dot(target, cc.groundNormal);
            // Grounding lags a tick, so a jump's first ticks would otherwise steer back into the floor.
            target.y = glm::max(target.y, velocity.y);
        } else {
            target.y = velocity.y;
        }

        // Capped, not exponential, so acceleration means m/s^2 at every speed.
        const float rate = cc.acceleration * (cc.grounded ? 1.0f : glm::max(cc.airControl, 0.0f));
        const glm::vec3 delta = target - velocity;
        const float distance = glm::length(delta);
        const float step = rate * dt;
        const bool tiny = distance <= Physics::CONTACT_TOLERANCE;
        const bool clamped = distance > step && !tiny;
        velocity += clamped ? delta * (step / distance) : delta;

        // Set, not accelerated toward: a metered climb is still building speed when its ground has gone.
        if (cc.stepping) velocity.y = stepClimb;

        // Otherwise the climb's last upward rate carries the character on up as if it jumped.
        if (wasStepping && !cc.stepping) velocity.y = glm::min(velocity.y, 0.0f);

        // Consumed even when not honoured, or a held jump fires on the next touch.
        if (cc.jumpRequested) {
            if (cc.grounded) {
                velocity.y = cc.jumpSpeed;
                // Now, not next tick: the supporting contact outlives separation, allowing a second jump.
                cc.grounded = false;
                // A running climb would set the vertical next tick, capping the jump at a kerb.
                cc.stepping = false;
            }
            cc.jumpRequested = false;
        }

        rb.linearVelocity = velocity;
    });

    if (sawNoCapsule && !m_noCapsuleLogged) {
        LOG_WARNING(
            "CharacterController: an entity has no enabled capsule Collider - "
            "it will not climb steps"
        );
    }
    if (sawSpinnable && !m_spinnableLogged) {
        LOG_WARNING(
            "CharacterController: an entity's Rigidbody has freezeRotation off - "
            "contacts will topple it"
        );
    }
    m_noCapsuleLogged = sawNoCapsule;
    m_spinnableLogged = sawSpinnable;
}

} // namespace Vkm::Engine
