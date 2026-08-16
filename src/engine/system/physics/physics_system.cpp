#include "system/physics/physics_system.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/clock.h"
#include "core/math/rotation.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/environment.h"
#include "ecs/component/collider.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/rigidbody.h"
#include "ecs/component/transform.h"
#include "ecs/component/world_transform.h"
#include "core/event/event_bus.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "system/physics/inertia.h"
#include "system/physics/physics_events.h"
#include "system/physics/collision/narrowphase.h"
#include "core/math/bounds.h"

namespace Engine {

namespace {

// Sleep thresholds: a body must rest (low speed while in contact) for this long
// before it stops simulating; a resting body wakes if struck by a faster one.
constexpr float SLEEP_LINEAR_SQ  = 0.04f;   // (0.2 m/s)^2
constexpr float SLEEP_ANGULAR_SQ = 0.04f;   // (0.2 rad/s)^2
constexpr float SLEEP_DELAY      = 0.5f;    // seconds of rest before sleeping
constexpr float WAKE_SPEED_SQ    = 0.25f;   // partner speed^2 that wakes a sleeper

float dynamicInverseMass(const Rigidbody& rb) {
    if (rb.isStatic || rb.isKinematic || rb.mass <= 0.0f) return 0.0f;
    return 1.0f / rb.mass;
}

// A body the integrator and solver leave alone: asleep or immovable (static,
// kinematic, or non-positive mass). Takes the tick's inverse mass rather than
// re-deriving it - dynamicInverseMass already folds static/kinematic/zero-mass
// into the value gatherBodies parked on the body frame.
bool isFrozen(const Rigidbody& rb, float invMass) {
    return rb.sleeping || invMass == 0.0f;
}

glm::mat3 localInverseInertia(const Rigidbody& rb, const Collider* collider) {
    // freezeRotation: infinite rotational inertia. Contact impulses then apply
    // zero torque in the solver, so the body translates but never tumbles -
    // the character-controller case (a runner shouldn't spin from a graze).
    if (rb.freezeRotation) return glm::mat3(0.0f);
    if (dynamicInverseMass(rb) == 0.0f || !collider || collider->parts.empty())
        return glm::mat3(0.0f);
    // Approximate the collider's inertia with a solid box of its overall local
    // extent - exact per-part inertia isn't worth it for gameplay.
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (const ColliderBox& part : collider->parts) {
        mn = glm::min(mn, part.center - part.halfExtents);
        mx = glm::max(mx, part.center + part.halfExtents);
    }
    // Inertia about the collider centre, parallel-axis-shifted to the entity
    // origin (where the solver measures contact arms) so an off-centre collider
    // gets its rotational response about the correct axis.
    const glm::mat3 inertia = boxInertiaLocal(rb.mass, (mx - mn) * 0.5f);
    if (inertia == glm::mat3(0.0f)) return glm::mat3(0.0f);
    const glm::vec3 center = (mx + mn) * 0.5f;
    const glm::mat3 shifted = glm::dot(center, center) > 0.0f
                                ? parallelAxisShift(inertia, rb.mass, center)
                                : inertia;
    return glm::inverse(shifted);
}

void computeAABB(
    const Collider& collider,
    const glm::vec3& pos,
    const glm::quat& rot,
    glm::vec3& outMin,
    glm::vec3& outMax
) {
    // Union of every child box's world AABB.
    if (collider.parts.empty()) { outMin = pos; outMax = pos; return; }
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());
    const glm::mat3 r = glm::mat3_cast(rot);
    glm::mat4 model = glm::mat4_cast(rot);  // rotation block is constant per body
    for (const ColliderBox& part : collider.parts) {
        model[3] = glm::vec4(pos + r * part.center, 1.0f);
        glm::vec3 mn, mx;
        Math::localToWorldAABB(model, -part.halfExtents, part.halfExtents, mn, mx);
        outMin = glm::min(outMin, mn);
        outMax = glm::max(outMax, mx);
    }
}

bool aabbOverlap(const ColliderProxy& a, const ColliderProxy& b) {
    return a.aabbMin.x <= b.aabbMax.x && a.aabbMax.x >= b.aabbMin.x
        && a.aabbMin.y <= b.aabbMax.y && a.aabbMax.y >= b.aabbMin.y
        && a.aabbMin.z <= b.aabbMax.z && a.aabbMax.z >= b.aabbMin.z;
}

void expandSubShapes(const ColliderProxy& p, const std::vector<ColliderBox>& parts,
                     std::vector<SubShape>& out) {
    out.clear();
    out.reserve(p.partsCount);
    const glm::mat3 r = glm::mat3_cast(p.rotation);
    for (uint32_t i = 0; i < p.partsCount; ++i) {
        const ColliderBox& part = parts[p.partsFirst + i];
        out.push_back({p.position + r * part.center, p.rotation, part.halfExtents});
    }
}

} // namespace

void PhysicsSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("PhysicsSystem");

    Scene& scene = ctx.scene;
    const float dt = ctx.clock.getFixedStep();

    // Scene-global like the Environment, but deliberately not part of it.
    const PhysicsSettings& physics = scene.physics();

    if (!gatherBodies(scene)) return;

    integrateForces(scene, physics, dt);

    broadphase();

    // hasContact spans narrowphase -> writeback (the sleep test reads it after the
    // solve), so it is owned here and threaded through both phases.
    std::vector<bool> hasContact(m_bodies.size(), false);
    narrowphase(hasContact, ctx.events);

    wakeOnImpact(scene);

    solve(physics, dt);

    writeback(scene, dt, hasContact);
}

bool PhysicsSystem::gatherBodies(Scene& scene) {
    PROFILE_SCOPE("Physics/Gather");

    auto* rbStorage = scene.storage<Rigidbody>();
    if (!rbStorage) return false;

    m_bodies.clear();
    m_solverBodies.clear();
    m_proxies.clear();
    m_proxyParts.clear();   // capacity kept: the parts are POD, so no per-body allocation
    m_bodyFrames.clear();

    const uint32_t rbCount = static_cast<uint32_t>(rbStorage->size());
    for (uint32_t i = 0; i < rbCount; ++i) {
        const uint32_t idx = rbStorage->keyAt(i);
        const EntityId id = scene.entityAt(idx);
        if (!scene.has<Transform>(id)) continue;

        Rigidbody& rb = rbStorage->dataAt(i);
        const Transform& t = scene.get<Transform>(id);
        const Collider* collider = scene.has<Collider>(id) ? &scene.get<Collider>(id) : nullptr;

        BodyFrame frame;
        // Defensively re-derive mass properties so editor edits to mass/collider
        // take effect without an explicit "apply" step.
        frame.invMass = dynamicInverseMass(rb);
        frame.invInertiaLocal = localInverseInertia(rb, collider);

        // Resolve the body's WORLD pose. A hierarchy root uses its local
        // Transform directly (fast path); a parented body reads its
        // WorldTransform and records its parent's frame so writeback can convert
        // the solved world pose back to local.
        glm::vec3 worldPos = t.position;
        glm::quat worldRot = t.rotation;
        if (scene.has<Hierarchy>(id)) {
            const Hierarchy& h = scene.get<Hierarchy>(id);
            if (h.parent && scene.has<WorldTransform>(id) && scene.has<WorldTransform>(h.parent)) {
                const glm::mat4& selfWorld   = scene.get<WorldTransform>(id).model;
                const glm::mat4& parentWorld = scene.get<WorldTransform>(h.parent).model;
                worldPos              = glm::vec3(selfWorld[3]);
                worldRot              = Math::worldRotationOf(selfWorld);
                frame.parented        = true;
                frame.parentWorldInv  = glm::inverse(parentWorld);
                frame.parentRot       = Math::worldRotationOf(parentWorld);
            }
        }

        frame.worldRot = worldRot;

        const uint32_t bodyIndex = static_cast<uint32_t>(m_bodies.size());
        m_bodies.push_back(id);
        m_bodyFrames.push_back(frame);

        PhysicsBody pb;
        pb.position = worldPos;
        pb.linearVelocity = rb.linearVelocity;
        // A rotation-frozen body also sheds any residual spin, so its
        // orientation integrates as identity and stays script-owned.
        pb.angularVelocity = rb.freezeRotation ? glm::vec3(0.0f) : rb.angularVelocity;
        // Sleeping or immovable bodies contribute infinite mass to the solver.
        const bool frozen = isFrozen(rb, frame.invMass);
        pb.invMass = frozen ? 0.0f : frame.invMass;
        pb.invInertiaWorld = frozen ? glm::mat3(0.0f)
                                    : inverseInertiaWorld(frame.invInertiaLocal, worldRot);
        pb.restitution = rb.restitution;
        pb.friction = rb.friction;
        m_solverBodies.push_back(pb);

        if (collider && collider->enabled) {
            ColliderProxy proxy;
            proxy.body = bodyIndex;
            proxy.partsFirst = static_cast<uint32_t>(m_proxyParts.size());
            proxy.partsCount = static_cast<uint32_t>(collider->parts.size());
            proxy.isTrigger  = collider->isTrigger;
            m_proxyParts.insert(m_proxyParts.end(), collider->parts.begin(), collider->parts.end());
            proxy.position = worldPos;
            proxy.rotation = worldRot;
            proxy.cullStatic = rb.isStatic || rb.isKinematic;
            computeAABB(*collider, worldPos, worldRot, proxy.aabbMin, proxy.aabbMax);
            m_proxies.push_back(proxy);
        }
    }

    return !m_bodies.empty();
}

void PhysicsSystem::integrateForces(Scene& scene, const PhysicsSettings& physics, float dt) {
    PROFILE_SCOPE("Physics/Integrate");

    for (size_t k = 0; k < m_bodies.size(); ++k) {
        Rigidbody& rb = scene.get<Rigidbody>(m_bodies[k]);
        PhysicsBody& pb = m_solverBodies[k];
        if (isFrozen(rb, m_bodyFrames[k].invMass)) continue;

        pb.linearVelocity += physics.gravity * rb.gravityScale * dt;
        pb.linearVelocity *= 1.0f / (1.0f + rb.linearDamping * dt);
        pb.angularVelocity *= 1.0f / (1.0f + rb.angularDamping * dt);
    }
}

void PhysicsSystem::broadphase() {
    PROFILE_SCOPE("Physics/Broadphase");

    m_sorted.clear();
    m_pairs.clear();

    for (uint32_t p = 0; p < m_proxies.size(); ++p) m_sorted.push_back(p);

    std::sort(m_sorted.begin(), m_sorted.end(), [&](uint32_t a, uint32_t b) {
        return m_proxies[a].aabbMin.x < m_proxies[b].aabbMin.x;
    });

    for (size_t a = 0; a < m_sorted.size(); ++a) {
        const ColliderProxy& pa = m_proxies[m_sorted[a]];
        for (size_t b = a + 1; b < m_sorted.size(); ++b) {
            const ColliderProxy& pb = m_proxies[m_sorted[b]];
            if (pb.aabbMin.x > pa.aabbMax.x) break;  // sorted on X: no further overlap
            if (pa.cullStatic && pb.cullStatic) continue;
            if (aabbOverlap(pa, pb)) m_pairs.emplace_back(m_sorted[a], m_sorted[b]);
        }
    }
}

void PhysicsSystem::narrowphase(std::vector<bool>& hasContact, EventBus& events) {
    PROFILE_SCOPE("Physics/Narrowphase");

    m_manifolds.clear();
    Contact scratch[MAX_CONTACTS_PER_MANIFOLD];

    // Colliders expand into their child boxes here, so a single body pair can
    // yield several manifolds (one per child box-pair that touches). The solver
    // already handles many manifolds per body pair, so this just works - each
    // manifold carries the same bodyA/bodyB indices.

    for (const auto& pair : m_pairs) {
        const ColliderProxy& A = m_proxies[pair.first];
        const ColliderProxy& B = m_proxies[pair.second];
        const bool trigger = A.isTrigger || B.isTrigger;

        expandSubShapes(A, m_proxyParts, m_subA);
        expandSubShapes(B, m_proxyParts, m_subB);

        bool anyContact = false;
        glm::vec3 contactPoint(0.0f);
        glm::vec3 contactNormal(0.0f, 1.0f, 0.0f);
        for (const SubShape& sa : m_subA) {
            for (const SubShape& sb : m_subB) {
                const int n = contactBoxes(
                    sa.center, sa.rotation, sa.halfExtents,
                    sb.center, sb.rotation, sb.halfExtents,
                    scratch);
                if (n == 0) continue;
                if (!anyContact) {  // keep the first contact as the event's representative
                    contactPoint  = scratch[0].point;
                    contactNormal = scratch[0].normal;
                }
                anyContact = true;
                if (trigger) continue;  // queried, not resolved

                ContactManifold manifold;
                manifold.bodyA = A.body;
                manifold.bodyB = B.body;
                manifold.count = std::min(n, MAX_CONTACTS_PER_MANIFOLD);
                for (int c = 0; c < manifold.count; ++c) manifold.contacts[c] = scratch[c];
                m_manifolds.push_back(manifold);
            }
        }
        if (anyContact) {
            // Surface the overlap to gameplay (enqueued: listeners fire on the
            // next EventBus flush, never mid-solve). Triggers are queried,
            // not resolved, so they only produce events.
            const EntityId entityA = m_bodies[A.body];
            const EntityId entityB = m_bodies[B.body];
            if (trigger) {
                if (A.isTrigger) events.enqueue(TriggerEvent{entityA, entityB});
                if (B.isTrigger) events.enqueue(TriggerEvent{entityB, entityA});
            } else {
                // Only a resolved contact counts as support for the sleep test.
                // A trigger holds nothing up, and a body that dozed off inside
                // one would be stuck: wakeOnImpact walks the manifolds, which
                // never carry trigger pairs.
                hasContact[A.body] = true;
                hasContact[B.body] = true;
                events.enqueue(CollisionEvent{entityA, entityB, contactPoint, contactNormal});
            }
        }
    }
}

void PhysicsSystem::wakeOnImpact(Scene& scene) {
    PROFILE_SCOPE("Physics/Wake");

    for (const ContactManifold& manifold : m_manifolds) {
        const uint32_t a = manifold.bodyA;
        const uint32_t b = manifold.bodyB;
        Rigidbody& rbA = scene.get<Rigidbody>(m_bodies[a]);
        Rigidbody& rbB = scene.get<Rigidbody>(m_bodies[b]);
        const float speedA = glm::dot(m_solverBodies[a].linearVelocity, m_solverBodies[a].linearVelocity);
        const float speedB = glm::dot(m_solverBodies[b].linearVelocity, m_solverBodies[b].linearVelocity);

        auto wake = [&](uint32_t idx, Rigidbody& rb) {
            rb.sleeping = false;
            rb.sleepTimer = 0.0f;
            m_solverBodies[idx].invMass = m_bodyFrames[idx].invMass;
            // The gathered world rotation, not the local Transform's: for a
            // parented body those differ, and the solver is running in world.
            m_solverBodies[idx].invInertiaWorld =
                inverseInertiaWorld(m_bodyFrames[idx].invInertiaLocal, m_bodyFrames[idx].worldRot);
        };
        if (rbA.sleeping && !rbB.sleeping && speedB > WAKE_SPEED_SQ) wake(a, rbA);
        if (rbB.sleeping && !rbA.sleeping && speedA > WAKE_SPEED_SQ) wake(b, rbB);
    }
}

void PhysicsSystem::solve(const PhysicsSettings& physics, float dt) {
    PROFILE_SCOPE("Physics/Solve");

    SolverParams params;
    params.iterations = physics.solverIterations;
    params.dt = dt;
    solveContacts(m_solverBodies, m_manifolds, params);
}

void PhysicsSystem::writeback(Scene& scene, float dt, const std::vector<bool>& hasContact) {
    PROFILE_SCOPE("Physics/Writeback");

    for (size_t k = 0; k < m_bodies.size(); ++k) {
        const EntityId id = m_bodies[k];
        Rigidbody& rb = scene.get<Rigidbody>(id);
        PhysicsBody& pb = m_solverBodies[k];

        if (rb.isStatic) continue;
        if (rb.sleeping) {
            rb.linearVelocity = glm::vec3(0.0f);
            rb.angularVelocity = glm::vec3(0.0f);
            continue;
        }

        rb.linearVelocity = pb.linearVelocity;
        rb.angularVelocity = pb.angularVelocity;

        Transform& t = scene.get<Transform>(id);
        const BodyFrame& frame = m_bodyFrames[k];
        const glm::vec3 linear = pb.linearVelocity + pb.pseudoLinear;
        const glm::vec3 angular = pb.angularVelocity + pb.pseudoAngular;

        // Integrate the pose in WORLD space (the frame the solver ran in), then
        // map it back to the entity's local Transform - an identity map for a
        // root, parent-relative for a parented body.
        const glm::vec3 worldPos = pb.position + linear * dt;
        const glm::quat worldRotOld = frame.parented ? frame.parentRot * t.rotation : t.rotation;
        const glm::quat spin(0.0f, angular.x, angular.y, angular.z);
        const glm::quat worldRot = glm::normalize(worldRotOld + 0.5f * spin * worldRotOld * dt);

        if (frame.parented) {
            t.position = glm::vec3(frame.parentWorldInv * glm::vec4(worldPos, 1.0f));
            t.rotation = glm::normalize(glm::conjugate(frame.parentRot) * worldRot);
        } else {
            t.position = worldPos;
            t.rotation = worldRot;
        }

        // Sleep bookkeeping: rest while supported long enough, otherwise stay
        // awake. canSleep opts a body out entirely - script-driven characters
        // must never doze off, or their velocity writes get zeroed and the
        // solver treats them as immovable mid-gameplay.
        if (!rb.isKinematic && rb.canSleep) {
            const float linSq = glm::dot(rb.linearVelocity, rb.linearVelocity);
            const float angSq = glm::dot(rb.angularVelocity, rb.angularVelocity);
            const bool resting = hasContact[k] && linSq < SLEEP_LINEAR_SQ && angSq < SLEEP_ANGULAR_SQ;
            if (resting) {
                rb.sleepTimer += dt;
                if (rb.sleepTimer >= SLEEP_DELAY) {
                    rb.sleeping = true;
                    rb.linearVelocity = glm::vec3(0.0f);
                    rb.angularVelocity = glm::vec3(0.0f);
                }
            } else {
                rb.sleepTimer = 0.0f;
            }
        }

        if (scene.has<Hierarchy>(id)) HierarchyOperations::markDirty(scene, id);
    }
}

} // namespace Engine
