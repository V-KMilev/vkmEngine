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
#include "system/physics/mass_model.h"
#include "system/physics/query/query.h"
#include "system/physics/solver/solver.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Squared speed, (m/s)^2, below which moveInput asks for no movement.
 *
 * A centimetre a second, so a stick a hair off centre does not start a step-up.
 */
constexpr float MOVE_SPEED_SQ = 1e-4f;

/**
 * @brief Speed, m/s, off ground it stood on past which a character was thrown rather than jostled.
 *
 * Where a contact starts to bounce: what the solve leaves at the foot of a slope stays under it, and
 * a pad or a blast is far past it.
 */
constexpr float THROW_SPEED = RESTITUTION_THRESHOLD;

/**
 * @brief Ground this close below a character grounded last tick holds it at any speed, in metres.
 *
 * Past what a hard landing's penetration recovery lifts it in a tick; short of a drop anyone would see.
 */
constexpr float HOLD_DISTANCE = 0.05f;

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
 * @param filter     What the probes may hit: what the character collides with (bodyFilter).
 * @param feet       World position of the bottom of the capsule.
 * @param radius     Capsule radius.
 * @param direction  Horizontal unit direction of travel.
 * @param stepHeight Tallest rise that may be mounted.
 * @param slopeLimit cos(maxSlopeAngle): at or above it the landing is walkable.
 * @param[out] rise  How far up the landing is, when there is one.
 * @return True when a step exists, is clear, and is walkable.
 */
bool findStep(
    Scene& scene,
    const QueryFilter& filter,
    const glm::vec3& feet,
    float radius,
    const glm::vec3& direction,
    float stepHeight,
    float slopeLimit,
    float& rise
) {
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

/**
 * @brief Shape the first capsule of @p collider to cc.crouched: shortened to crouchHeight with its feet
 *        where they were, or back to the height it stood at.
 *
 * @param cc       The controller; standingHalfHeight keeps the standing height while short.
 * @param collider The character's collider.
 */
void fitCapsule(CharacterController& cc, Collider& collider) {
    for (ColliderPart& part : collider.parts) {
        if (part.shape != ColliderShape::Capsule) continue;
        const bool isShort = cc.standingHalfHeight > 0.0f;
        if (cc.crouched && !isShort && part.halfHeight > 0.0f) {
            const float low = glm::clamp(cc.crouchHeight * 0.5f - part.radius, 0.0f, part.halfHeight);
            cc.standingHalfHeight = part.halfHeight;
            part.center.y        -= part.halfHeight - low;
            part.halfHeight       = low;
        } else if (!cc.crouched && isShort) {
            part.center.y         += cc.standingHalfHeight - part.halfHeight;
            part.halfHeight        = cc.standingHalfHeight;
            cc.standingHalfHeight  = 0.0f;
        }
        return;
    }
}

/**
 * @brief What a character's own probes may hit: what its body collides with, both ways, and not
 *        itself or its ragdoll, whose shin would block every staircase.
 *
 * @param self The character.
 * @param rb   Its body.
 * @return The filter its probes cast through.
 */
QueryFilter bodyFilter(EntityId self, const Rigidbody& rb) {
    QueryFilter filter;
    filter.ignore      = self;
    filter.layerMask   = rb.collidesWith;
    filter.casterLayer = rb.layer;
    return filter;
}

/**
 * @brief Whether a crouched character has room over its head to stand.
 *
 * Its top cap swept up by what standing adds, a shade narrower than the capsule, so a wall touching its
 * sides is not over its head.
 *
 * @param scene      Scene to query.
 * @param filter     What the sweep may hit: what the character collides with (bodyFilter).
 * @param feet       World position of the bottom of the capsule.
 * @param radius     Capsule radius.
 * @param halfHeight Its halfHeight crouched.
 * @param standing   Its halfHeight standing.
 * @return True when nothing is in the way.
 */
bool roomToStand(
    Scene& scene,
    const QueryFilter& filter,
    const glm::vec3& feet,
    float radius,
    float halfHeight,
    float standing
) {
    const glm::vec3 top = feet + Math::WORLD_UP * (radius + 2.0f * halfHeight);
    RayHit hit;
    const float rise = 2.0f * (standing - halfHeight);
    return !spherecast(scene, top, radius * 0.95f, Math::WORLD_UP, rise, hit, filter);
}

/**
 * @brief Whether walkable ground lies close enough below a character that has lost it to hold it.
 *
 * The capsule's bottom sphere swept down, a shade narrower so a wall touching its side is not under it;
 * the gap reported is the whole capsule's.
 *
 * @param scene      Scene to query.
 * @param filter     What the sweep may hit: what the character collides with (bodyFilter).
 * @param feet       World position of the bottom of the capsule.
 * @param radius     Capsule radius.
 * @param reach      The furthest the ground may have dropped, in metres.
 * @param slopeLimit cos(maxSlopeAngle): at or above it the ground is walkable.
 * @param[out] hit   The ground met.
 * @param[out] gap   How far the capsule is above it, along its normal.
 * @return True when walkable ground is within @p reach straight down.
 */
bool findGround(
    Scene& scene,
    const QueryFilter& filter,
    const glm::vec3& feet,
    float radius,
    float reach,
    float slopeLimit,
    RayHit& hit,
    float& gap
) {
    if (reach <= 0.0f) return false;
    const float probeRadius = radius * 0.95f;
    const float shrink      = radius - probeRadius;
    const glm::vec3 centre  = feet + Math::WORLD_UP * radius;
    if (!spherecast(scene, centre, probeRadius, -Math::WORLD_UP, reach + radius, hit, filter)) return false;

    const float upright = glm::dot(hit.normal, Math::WORLD_UP);
    if (upright < slopeLimit) return false;
    // The narrower sphere meets a plane later than the capsule would, by the shrink along its normal.
    const float along = hit.distance * upright - shrink;
    if (along > reach * upright) return false;
    gap = glm::max(along, 0.0f);
    return true;
}

/**
 * @brief How fast the body @p under carries a point at @p feet, and how fast it turns about up.
 *
 * The point's velocity as the contact solver takes it, so the two never pull a rider apart.
 *
 * @param scene Scene the body is in.
 * @param under The body stood on.
 * @param feet  World position of the rider's feet.
 * @param[out] spin Its spin about up, rad/s.
 * @return The velocity at the feet; zero for a static body or none.
 */
glm::vec3 carriedBy(Scene& scene, EntityId under, const glm::vec3& feet, float& spin) {
    spin = 0.0f;
    const Rigidbody* body = scene.tryGet<Rigidbody>(under);
    const Transform* local = scene.tryGet<Transform>(under);
    if (!body || !local || body->motion == RigidbodyMotion::Static) return glm::vec3(0.0f);

    // Its velocity is its solver centre's, which it turns about.
    const BodyPose  pose   = worldPoseOf(scene, under, *local);
    const glm::vec3 centre = solverCentre(*body, body->motion, scene.tryGet<Collider>(under));
    const glm::vec3 lever  = feet - (pose.position + pose.rotation * centre);
    spin = glm::dot(body->angularVelocity, Math::WORLD_UP);
    return body->linearVelocity + glm::cross(body->angularVelocity, lever);
}

// Of moveInput's speed along a face being climbed, the share kept: a climber edges sideways.
constexpr float CLIMB_SIDE_SHARE = 0.5f;
// Of jumpSpeed, the share a leap off a face goes out from it at.
constexpr float CLIMB_LEAP_SHARE = 0.5f;

/**
 * @brief The velocity a climbing character closes on: up or down the face for the part of
 *        @p flatInput into or away from it, along it for the rest.
 *
 * @param flatInput   moveInput, level.
 * @param climbNormal Out of the face; only its level part counts.
 * @param climbSpeed  The fastest up or down.
 * @param[out] out    The level unit normal used.
 * @return The target velocity.
 */
glm::vec3 climbTarget(
    const glm::vec3& flatInput,
    const glm::vec3& climbNormal,
    float climbSpeed,
    glm::vec3& out
) {
    out = {climbNormal.x, 0.0f, climbNormal.z};
    const float lengthSq = glm::dot(out, out);
    out = lengthSq > Physics::DEGENERATE_SQ ? out / std::sqrt(lengthSq) : glm::vec3(0.0f, 0.0f, 1.0f);
    const float     into  = -glm::dot(flatInput, out);
    const glm::vec3 along = flatInput + out * into;
    return Math::WORLD_UP * glm::clamp(into, -climbSpeed, climbSpeed) + along * CLIMB_SIDE_SHARE;
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

    scene.forEach<CharacterController, Rigidbody>([&](EntityId id, CharacterController& cc, Rigidbody& rb) {
        // Left where the authority put it, grounded and crouched included (see netEncode for
        // CharacterController); its capsule follows what it was told.
        Collider* shape = scene.tryGet<Collider>(id);
        if (!ctx.net.simulates(scene, id)) {
            if (shape) fitCapsule(cc, *shape);
            return;
        }
        // A body not Dynamic is placed by script, not moved by velocity: there is nothing to steer, and
        // nothing of walking carries over - no jump pressed meanwhile fires once it moves again.
        if (rb.motion != RigidbodyMotion::Dynamic) {
            cc.grounded       = false;
            cc.groundNormal   = Math::WORLD_UP;
            cc.groundVelocity = glm::vec3(0.0f);
            cc.groundSpin     = 0.0f;
            cc.stepping       = false;
            cc.jumping        = false;
            cc.climbing       = false;
            cc.jumpRequested  = false;
            cc.walkSpeed      = 0.0f;
            return;
        }

        // A wall is a resolved contact too, and standing on one is not standing.
        const float slopeLimit = std::cos(glm::radians(glm::clamp(cc.maxSlopeAngle, 0.0f, 90.0f)));
        const bool touching = rb.supported && glm::dot(rb.supportNormal, Math::WORLD_UP) >= slopeLimit;
        // Last tick's ground, what a crest or a throw is measured from. A snapshot may have set whether it
        // stood, never what on: a replay's first tick takes the normal and carry of the present it rewound.
        const bool      wasGrounded = cc.grounded;
        const glm::vec3 lastNormal  = cc.groundNormal;
        cc.grounded     = touching;
        cc.groundNormal = touching ? rb.supportNormal : Math::WORLD_UP;

        // The capsule as `crouched` says first, which a snapshot may have changed: standing up from a
        // crouch the authority told of still asks for room.
        if (shape) fitCapsule(cc, *shape);

        float     radius     = 0.0f;
        float     halfHeight = 0.0f;
        glm::vec3 offset(0.0f);
        const bool standsOnCapsule = shape && shape->enabled && capsuleOf(*shape, radius, halfHeight, offset);

        if (!standsOnCapsule && m_noCapsule.report()) {
            LOG_WARNING(
                "CharacterController: an entity has no enabled capsule Collider - "
                "it will not climb steps"
            );
        }
        if (!rb.freezeRotation && m_spinnable.report()) {
            LOG_WARNING(
                "CharacterController: an entity's Rigidbody has freezeRotation off - "
                "contacts will topple it"
            );
        }

        const glm::vec3 flatInput = {cc.moveInput.x, 0.0f, cc.moveInput.z};

        const Transform* local = scene.tryGet<Transform>(id);
        const bool climbable = standsOnCapsule && local && radius > 0.0f;

        // World space, as stepTargetY and the probes are; a parented character's Transform is not.
        glm::vec3 feet(0.0f);
        if (climbable) {
            const BodyPose pose = worldPoseOf(scene, id, *local);
            feet = pose.position + pose.rotation * offset
                - Math::WORLD_UP * (halfHeight + radius);
        }

        // Down at once; up only with room over its head. The capsule changes for the next tick's solve.
        if (cc.crouching) {
            cc.crouched = true;
        } else if (cc.crouched) {
            const float       standing = cc.standingHalfHeight;
            const bool        sized    = climbable && standing > 0.0f;
            const QueryFilter probes   = bodyFilter(id, rb);
            cc.crouched = sized && !roomToStand(scene, probes, feet, radius, halfHeight, standing);
        }
        if (shape) fitCapsule(cc, *shape);

        // What it stands on carries it: steering is relative to that, and the carry added back after.
        const glm::vec3 lastCarry = cc.groundVelocity;
        float     spin = 0.0f;
        glm::vec3 carry(0.0f);
        if (touching && climbable) carry = carriedBy(scene, rb.supportBody, feet, spin);

        // How fast it leaves its ground: the least against every walkable surface touched and the plane
        // it walked last tick, so neither a crest nor the foot of a ramp reads as a throw.
        const glm::vec3 overLast = rb.linearVelocity - lastCarry;
        const glm::vec3 overNow  = rb.linearVelocity - carry;
        float leaving = touching ? glm::dot(overNow, rb.supportNormal) : rb.linearVelocity.y;
        if (touching && glm::dot(rb.blockNormal, Math::WORLD_UP) >= slopeLimit) {
            leaving = glm::min(leaving, glm::dot(overNow, rb.blockNormal));
        }
        if (wasGrounded) {
            const float offLast = glm::dot(overLast, lastNormal);
            leaving = touching ? glm::min(leaving, offLast) : offLast;
        }

        // A throw lasts until gravity is about to turn it. Ground it stood on throws it by being left past
        // THROW_SPEED; a landing's bounce never does.
        if (cc.jumping) {
            const float gravityTick = glm::max(-glm::dot(scene.physics().gravity, Math::WORLD_UP), 0.0f)
                * rb.gravityScale * dt;
            cc.jumping = leaving > gravityTick;
        } else if (wasGrounded && !cc.stepping) {
            cc.jumping = leaving > THROW_SPEED;
        }

        // Ground falling away under its feet holds it, as far as twice what the steepest walkable slope
        // drops at its speed in a tick - off a crest, down a slope it was not steering along - or
        // HOLD_DISTANCE.
        bool  snapped = false;
        float gap     = 0.0f;
        if (cc.jumping) {
            cc.grounded     = false;
            cc.groundNormal = Math::WORLD_UP;
        } else if (!touching && wasGrounded && !cc.stepping && !cc.climbing && climbable) {
            const float across = glm::length(glm::vec2(overLast.x, overLast.z));
            const float slope  = std::sqrt(glm::max(1.0f - slopeLimit * slopeLimit, 0.0f));
            const float fall   = 2.0f * across * dt * slope / glm::max(slopeLimit, glm::epsilon<float>());
            RayHit ground;
            snapped = findGround(
                scene,
                bodyFilter(id, rb),
                feet,
                radius,
                glm::min(glm::max(fall, HOLD_DISTANCE), cc.stepHeight),
                slopeLimit,
                ground,
                gap
            );
            if (snapped) {
                cc.grounded     = true;
                cc.groundNormal = ground.normal;
                carry           = carriedBy(scene, ground.entity, feet, spin);
            }
        }
        cc.groundVelocity = cc.grounded ? carry : glm::vec3(0.0f);
        cc.groundSpin     = cc.grounded ? spin : 0.0f;

        // On a face, nothing of walking applies: steered toward the climb at `acceleration`, with the
        // gravity the step to come adds (PhysicsSystem's integrateForces) taken off ahead.
        if (cc.climbing) {
            cc.stepping = false;
            glm::vec3       out(0.0f);
            const glm::vec3 target   = climbTarget(flatInput, cc.climbNormal, cc.climbSpeed, out);
            glm::vec3       velocity = rb.linearVelocity;
            const glm::vec3 delta    = target - velocity;
            const float     distance = glm::length(delta);
            const float     step     = cc.acceleration * dt;
            velocity += distance > step ? delta * (step / distance) : delta;
            cc.jumping = cc.jumpRequested;
            if (cc.jumpRequested) {
                velocity = Math::WORLD_UP * cc.jumpSpeed + out * (CLIMB_LEAP_SHARE * cc.jumpSpeed);
            } else {
                velocity -= scene.physics().gravity * rb.gravityScale * dt;
            }
            // Held for this tick alone: a character whose hold is gone falls rather than hangs.
            cc.climbing       = false;
            cc.jumpRequested  = false;
            cc.walkSpeed      = glm::length(glm::vec3(velocity.x, 0.0f, velocity.z));
            rb.linearVelocity = velocity;
            return;
        }
        const float inputLenSq = glm::dot(flatInput, flatInput);
        const bool blocked = glm::dot(rb.blockNormal, Math::WORLD_UP) < slopeLimit;

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
                bodyFilter(id, rb),
                feet,
                radius,
                forward,
                cc.stepHeight,
                slopeLimit,
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

        glm::vec3 velocity = rb.linearVelocity - cc.groundVelocity;
        // Deflected before the ground projection, so the slope's vertical follows the final direction.
        glm::vec3 target = cc.stepping
            ? cc.moveInput
            : deflectAlongBlock(cc.moveInput, rb.blockNormal, slopeLimit);

        // Grounded, the target follows the ground plane, down a slope as well as up it. Airborne, the
        // vertical is gravity's alone.
        if (cc.grounded) {
            target -= cc.groundNormal * glm::dot(target, cc.groundNormal);
        } else {
            target.y = velocity.y;
        }

        // Capped, not exponential, so acceleration means m/s^2 at every speed.
        float rate = cc.acceleration * (cc.grounded ? 1.0f : glm::max(cc.airControl, 0.0f));

        // A shove - horizontal speed past both what is asked for and what this steering last left - is
        // carried, bleeding off at knockbackDrag, and steered only as in the air while it lasts.
        const glm::vec3 flatVelocity = {velocity.x, 0.0f, velocity.z};
        const float     speed        = glm::length(flatVelocity);
        const float     wanted       = glm::length(glm::vec3(target.x, 0.0f, target.z));
        // A change in what carries it - landing on a moving deck - is no shove either.
        const glm::vec3 carryChange  = cc.groundVelocity - lastCarry;
        const float     newCarry     = glm::length(glm::vec2(carryChange.x, carryChange.z));
        const float     kept         = glm::max(wanted, cc.walkSpeed + newCarry);
        const float     carried      = speed - kept;
        const bool      shoved       = cc.knockbackDrag > 0.0f && carried > Physics::CONTACT_TOLERANCE;
        if (shoved) {
            velocity -= flatVelocity * (glm::min(carried, cc.knockbackDrag * dt) / speed);
            rate = cc.acceleration * glm::max(cc.airControl, 0.0f);
        }
        const glm::vec3 delta = target - velocity;
        const float distance = glm::length(delta);
        const float step = rate * dt;
        const bool tiny = distance <= Physics::CONTACT_TOLERANCE;
        const bool clamped = distance > step && !tiny;
        velocity += clamped ? delta * (step / distance) : delta;

        // Held to its ground: what would leave it - a landing's bounce, a crest's throw - is taken back,
        // and a gap it is held over closes in a tick. A climb sets its own vertical.
        if (cc.grounded && !cc.stepping) {
            const float off  = glm::dot(velocity, cc.groundNormal);
            const float keep = snapped ? -gap / dt : glm::min(off, 0.0f);
            velocity += cc.groundNormal * (keep - off);
        }

        // Set, not accelerated toward: a metered climb is still building speed when its ground has gone.
        if (cc.stepping) velocity.y = stepClimb;

        // Otherwise the climb's last upward rate carries the character on up as if it jumped.
        if (wasStepping && !cc.stepping) velocity.y = glm::min(velocity.y, 0.0f);

        // Consumed even when not honoured, or a held jump fires on the next touch.
        if (cc.jumpRequested) {
            if (cc.grounded) {
                velocity.y = cc.jumpSpeed;
                // Now, and held so by `jumping`: the supporting contact outlives separation, and would
                // ground it again next tick.
                cc.grounded = false;
                cc.jumping  = true;
                // A running climb would set the vertical next tick, capping the jump at a kerb.
                cc.stepping = false;
            }
            cc.jumpRequested = false;
        }

        // What steering left, less any shove still carried, is the walk the next shove is measured from.
        const float left = glm::length(glm::vec3(velocity.x, 0.0f, velocity.z));
        cc.walkSpeed     = shoved ? glm::min(left, kept) : left;
        rb.linearVelocity = velocity + cc.groundVelocity;
    });

    m_noCapsule.endPass();
    m_spinnable.endPass();
}

} // namespace Vkm::Engine
