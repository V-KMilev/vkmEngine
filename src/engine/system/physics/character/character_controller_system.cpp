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

constexpr float MOVE_SPEED_SQ = 1e-4f;

/**
 * @brief Turn @p target so it runs along the surface the body is pressed
 *        against instead of into it.
 *
 * A surface within the slope limit is ground rather than an obstacle and the
 * caller's ground projection follows it; only something steeper deflects, and
 * that one is taken flat - a wall that turned the target upward would be a ramp
 * the slope limit exists to refuse. World up is what PhysicsSystem publishes when
 * a body touched nothing, and it is walkable at every slope limit, so no separate
 * "is there a contact" test is needed.
 *
 * Sliding along a wall is the controller's job rather than the solver's because
 * steering into one makes a normal force, friction scales with it, and whether
 * the character glides or stops dead then depends on two material numbers. A
 * target already running along the wall never pushes into it.
 *
 * @param target Desired velocity, world space.
 * @param blockNormal Rigidbody::blockNormal - the most horizontal contact normal.
 * @param slopeLimit cos(maxSlopeAngle): at or above it the surface is walkable.
 * @return The target with any component pushing into that surface removed.
 */
glm::vec3 deflectAlongBlock(const glm::vec3& target, const glm::vec3& blockNormal,
                            float slopeLimit) {
    if (glm::dot(blockNormal, Math::WORLD_UP) >= slopeLimit) return target;

    const glm::vec3 flat = {blockNormal.x, 0.0f, blockNormal.z};
    const float lengthSq = glm::dot(flat, flat);
    // A ceiling is unwalkable and has no sideways direction to slide along.
    if (lengthSq <= Physics::DEGENERATE_SQ) return target;

    const glm::vec3 axis = flat / std::sqrt(lengthSq);
    return target - axis * glm::min(0.0f, glm::dot(target, axis));
}

/**
 * @brief The first capsule part of a collider, which is what a character
 *        stands on.
 *
 * @param collider Collider to search.
 * @param[out] radius     Its sweep radius.
 * @param[out] halfHeight Half its segment, caps excluded.
 * @param[out] center     Its offset from the entity origin, body-local.
 * @return False when the collider has no capsule, which the caller warns about.
 */
bool capsuleOf(const Collider& collider, float& radius, float& halfHeight,
               glm::vec3& center) {
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
 * Two sweeps, in the order that makes the cheap rejection first. One forward at
 * step height asks whether there is room up there at all; a character that would
 * climb into the underside of a shelf must not start. One down behind it asks
 * what it would land on, and refuses anything it could not have stood on had it
 * walked there.
 *
 * The probes are a shade narrower than the capsule so that the geometry already
 * touching its sides - the very obstacle being tested - does not report itself
 * as the thing in the way.
 *
 * @param scene      Scene to query.
 * @param self       The character, excluded from its own probes.
 * @param feet       World position of the bottom of the capsule.
 * @param radius     Capsule radius.
 * @param direction  Horizontal unit direction of travel.
 * @param stepHeight Tallest rise that may be mounted.
 * @param slopeLimit cos(maxSlopeAngle): at or above it the landing is walkable.
 * @param layerMask  Layers the probes may hit - the body's own collision mask.
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
    // What the body can collide with, nothing more: a ragdoll's bones ride the
    // animated legs on a layer the body passes through, and a probe that can
    // hit its own shin reports every staircase as blocked.
    filter.layerMask = layerMask;

    const float probeRadius = radius * 0.9f;
    const float reach = radius * 2.0f;

    // Centre of a sphere sitting just above the step, at the height the
    // character would have to reach to clear it.
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

// How long mounting a step takes, in seconds. A step is not a jump and must not
// read as one, and it must not depend on how fast the character happens to be
// walking.
constexpr float STEP_CLIMB_TIME = 0.12f;

// And how long one may take before it is abandoned. Three times the budget: far
// past any climb that is still making progress, short enough that a character
// held against something it cannot mount does not hang there.
constexpr float STEP_CLIMB_LIMIT = STEP_CLIMB_TIME * 3.0f;

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
        // A character this end does not decide is left where the authority put
        // it, standing included - which is why grounded is set below this gate.
        // The CharacterController codec says why it cannot be worked out here.
        if (!ctx.net.simulates(id)) return;

        // A wall is a resolved contact too, and standing on one is not standing.
        const float slopeLimit = std::cos(glm::radians(glm::clamp(cc.maxSlopeAngle, 0.0f, 90.0f)));
        cc.grounded = rb.supported && glm::dot(rb.supportNormal, Math::WORLD_UP) >= slopeLimit;
        cc.groundNormal = cc.grounded ? rb.supportNormal : Math::WORLD_UP;

        const Collider* collider = scene.tryGet<Collider>(id);
        if (!collider || !collider->enabled || !hasCapsule(*collider)) sawNoCapsule = true;
        if (!rb.freezeRotation) sawSpinnable = true;

        // A body that dozed off has its velocity zeroed by the solver's
        // writeback, so anything asking it to move has to wake it first.
        const float asked = glm::dot(cc.moveInput, cc.moveInput);
        const bool wants = asked > MOVE_SPEED_SQ || cc.jumpRequested;
        if (wants && rb.sleeping) {
            rb.sleeping = false;
            rb.sleepTimer = 0.0f;
        }

        // Deflected before the ground projection, not after: this only turns
        // the horizontal, so the slope-following vertical below is computed for
        // the direction the character ends up going.
        const glm::vec3 flatInput = {cc.moveInput.x, 0.0f, cc.moveInput.z};
        const float inputLenSq = glm::dot(flatInput, flatInput);
        const bool blocked = glm::dot(rb.blockNormal, Math::WORLD_UP) < slopeLimit;

        float radius = 0.0f;
        float halfHeight = 0.0f;
        glm::vec3 offset(0.0f);
        const bool climbable = collider && scene.has<Transform>(id)
                            && capsuleOf(*collider, radius, halfHeight, offset)
                            && radius > 0.0f;

        // Resolved to world space, because stepTargetY is a world height and the
        // probes are cast there: a character parented to a lift or a prefab root
        // has a Transform in that parent's frame.
        glm::vec3 feet(0.0f);
        if (climbable) {
            const BodyPose pose = worldPoseOf(scene, id, scene.get<Transform>(id));
            feet = pose.position + pose.rotation * offset
                 - Math::WORLD_UP * (halfHeight + radius);
        }

        // A climb ends by arriving, by the character no longer asking, or by its
        // deadline - never because the step stopped blocking, since rising is
        // what un-blocks a riser.
        const bool wasStepping = cc.stepping;
        if (cc.stepping) {
            cc.stepTime += dt;
            const float reached = cc.stepTargetY - Physics::CONTACT_TOLERANCE;
            const bool arrived = climbable && feet.y >= reached;
            const bool asking = inputLenSq > MOVE_SPEED_SQ;
            // A resting contact settles a slop short of the measured height,
            // so "arrived" is approached and never met; the clock is what
            // keeps that from holding the character up forever.
            const bool expired = cc.stepTime >= STEP_CLIMB_LIMIT;
            if (!climbable || !asking || arrived || expired) cc.stepping = false;
        }

        // Starting one asks for both: on the ground, walking into something
        // that blocks. The probes cost two sweeps and answer nothing for a
        // character standing still or already sliding freely.
        if (!cc.stepping && climbable && cc.grounded && blocked
                && cc.stepHeight > 0.0f && inputLenSq > MOVE_SPEED_SQ) {
            const glm::vec3 forward = flatInput / std::sqrt(inputLenSq);
            float rise = 0.0f;
            if (findStep(scene, id, feet, radius, forward, cc.stepHeight,
                         slopeLimit, rb.collidesWith, rise)) {
                cc.stepping = true;
                cc.stepTime = 0.0f;
                cc.stepTargetY = feet.y + rise;
            }
        }

        // The rate that closes what is left, capped so the first tick does not
        // teleport. Driven by the remaining distance rather than the step limit,
        // so a kerb rises by a kerb and not by a jump's worth of speed.
        const float remaining = climbable ? cc.stepTargetY - feet.y : 0.0f;
        const float stepClimb = (cc.stepping && dt > 0.0f && remaining > 0.0f)
            ? glm::min(remaining / dt, cc.stepHeight / STEP_CLIMB_TIME)
            : 0.0f;

        glm::vec3 velocity = rb.linearVelocity;
        glm::vec3 target = cc.stepping
            ? cc.moveInput
            : deflectAlongBlock(cc.moveInput, rb.blockNormal, slopeLimit);

        // Grounded, the target follows the ground plane, so walking up a ramp
        // neither launches off the top nor is dragged back by gravity fighting
        // the contact. Airborne, the vertical is gravity's alone.
        if (cc.grounded) {
            target -= cc.groundNormal * glm::dot(target, cc.groundNormal);
            // Already rising faster than the ground would carry it: it has just
            // jumped or been thrown. Grounding lags the contacts by a tick, so
            // without this the first ticks of a jump steer back into the floor.
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
        const bool tiny = distance <= Physics::CONTACT_TOLERANCE;
        const bool clamped = distance > step && !tiny;
        velocity += clamped ? delta * (step / distance) : delta;

        // Set rather than steered toward: the acceleration limit describes how
        // fast a character changes its mind, and a climb metered through it is
        // still building speed when the ground it needs has gone.
        if (cc.stepping) velocity.y = stepClimb;

        // A step ends by arriving, not by launching. Without this the tick that
        // finishes one leaves the climb's last upward rate on the body, and the
        // character carries on up as though it had jumped.
        if (wasStepping && !cc.stepping) velocity.y = glm::min(velocity.y, 0.0f);

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
