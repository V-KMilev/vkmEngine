#include "system/physics/physics_system.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/collider.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/physics_world.h"
#include "ecs/component/rigidbody.h"
#include "ecs/component/transform.h"
#include "ecs/component/world_transform.h"
#include "system/event/event_system.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "system/physics/inertia.h"
#include "system/physics/physics_events.h"
#include "system/physics/collision/narrowphase.h"
#include "system/visibility/bounds_utils.h"

namespace Engine {

namespace {

// Sleep thresholds: a body must rest (low speed while in contact) for this long
// before it stops simulating; a resting body wakes if struck by a faster one.
constexpr float SLEEP_LINEAR_SQ  = 0.04f;   // (0.2 m/s)^2
constexpr float SLEEP_ANGULAR_SQ = 0.04f;   // (0.2 rad/s)^2
constexpr float SLEEP_DELAY      = 0.5f;    // seconds of rest before sleeping
constexpr float WAKE_SPEED_SQ    = 0.25f;   // partner speed^2 that wakes a sleeper

/**
 * @brief Broadphase / narrowphase view of one collidable body, cached per tick.
 * `body` indexes the parallel solver-body array; `cullStatic` is true only for
 * permanently immovable bodies (static/kinematic) - a sleeping dynamic body is
 * NOT cullStatic, so it keeps generating contacts and stays supported.
 */
struct ColliderProxy {
    uint32_t body = 0;
    Collider collider;
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 aabbMin = {0.0f, 0.0f, 0.0f};
    glm::vec3 aabbMax = {0.0f, 0.0f, 0.0f};
    bool cullStatic = false;
};

float dynamicInverseMass(const Rigidbody& rb) {
    if (rb.isStatic || rb.isKinematic || rb.mass <= 0.0f) return 0.0f;
    return 1.0f / rb.mass;
}

glm::mat3 localInverseInertia(const Rigidbody& rb, const Collider* collider) {
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
    for (const ColliderBox& part : collider.parts) {
        glm::mat4 model = glm::mat4_cast(rot);
        model[3] = glm::vec4(pos + r * part.center, 1.0f);
        glm::vec3 mn, mx;
        localToWorldAABB(model, -part.halfExtents, part.halfExtents, mn, mx);
        outMin = glm::min(outMin, mn);
        outMax = glm::max(outMax, mx);
    }
}

/**
 * @brief World-space pose frame for a body, cached at gather so writeback can
 *        map the solved world pose back to the entity's local Transform.
 *
 * `parented == false` means the body is a hierarchy root (its local Transform is
 * already the world pose - the fast path, no conversion).
 */
struct BodyFrame {
    bool      parented       = false;
    glm::mat4 parentWorldInv = glm::mat4(1.0f);                    ///< inverse(parent WorldTransform.model)
    glm::quat parentRot      = {1.0f, 0.0f, 0.0f, 0.0f};           ///< parent world-space rotation
};

/// Rotation of a world matrix, scale-tolerant (normalises the basis columns so a
/// uniformly/non-uniformly scaled parent still yields the correct rotation).
glm::quat worldRotationOf(const glm::mat4& m) {
    glm::mat3 basis(m);
    basis[0] = glm::normalize(basis[0]);
    basis[1] = glm::normalize(basis[1]);
    basis[2] = glm::normalize(basis[2]);
    return glm::normalize(glm::quat_cast(basis));
}

bool aabbOverlap(const ColliderProxy& a, const ColliderProxy& b) {
    return a.aabbMin.x <= b.aabbMax.x && a.aabbMax.x >= b.aabbMin.x
        && a.aabbMin.y <= b.aabbMax.y && a.aabbMax.y >= b.aabbMin.y
        && a.aabbMin.z <= b.aabbMax.z && a.aabbMax.z >= b.aabbMin.z;
}

/**
 * @brief A box placed in world space - the unit the box-box narrowphase consumes.
 * A collider expands into one of these per child box.
 */
struct SubShape {
    glm::vec3 center      = {0.0f, 0.0f, 0.0f};
    glm::quat rotation    = {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};
};

void expandSubShapes(const ColliderProxy& p, std::vector<SubShape>& out) {
    out.clear();
    out.reserve(p.collider.parts.size());
    const glm::mat3 r = glm::mat3_cast(p.rotation);
    for (const ColliderBox& part : p.collider.parts) {
        out.push_back({p.position + r * part.center, p.rotation, part.halfExtents});
    }
}

} // namespace

void PhysicsSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("PhysicsSystem");

    Scene& scene = ctx.scene;
    const float dt = ctx.fixedDeltaTime;

    // Per-scene physics settings (singleton component); defaults if absent.
    PhysicsWorld world;
    scene.forEach<PhysicsWorld>([&](EntityId, const PhysicsWorld& w) { world = w; });

    auto* rbStorage = scene.storage<Rigidbody>();
    if (!rbStorage) return;

    // Gather: snapshot every live Rigidbody+Transform into solver state.
    m_bodies.clear();
    m_solverBodies.clear();

    thread_local std::vector<ColliderProxy> proxies;
    proxies.clear();

    // Per-body world<->local frame, parallel to m_bodies, so writeback can map
    // the solved world pose back into a parented body's local Transform.
    thread_local std::vector<BodyFrame> bodyFrames;
    bodyFrames.clear();

    const uint32_t rbCount = static_cast<uint32_t>(rbStorage->size());
    for (uint32_t i = 0; i < rbCount; ++i) {
        const uint32_t idx = rbStorage->keyAt(i);
        const EntityId id{idx, scene.generationOf(idx)};
        if (!scene.has<Transform>(id)) continue;

        Rigidbody& rb = rbStorage->dataAt(i);
        const Transform& t = scene.get<Transform>(id);
        const Collider* collider = scene.has<Collider>(id) ? &scene.get<Collider>(id) : nullptr;

        // Defensively re-derive mass properties so editor edits to mass/collider
        // take effect without an explicit "apply" step.
        rb.inverseMass = dynamicInverseMass(rb);
        rb.invInertiaLocal = localInverseInertia(rb, collider);

        // Resolve the body's WORLD pose. A hierarchy root uses its local
        // Transform directly (fast path); a parented body reads its
        // WorldTransform and records its parent's frame so writeback can convert
        // the solved world pose back to local.
        glm::vec3 worldPos = t.position;
        glm::quat worldRot = t.rotation;
        BodyFrame frame;
        if (scene.has<Hierarchy>(id)) {
            const Hierarchy& h = scene.get<Hierarchy>(id);
            if (h.parent && scene.has<WorldTransform>(id) && scene.has<WorldTransform>(h.parent)) {
                const glm::mat4& selfWorld   = scene.get<WorldTransform>(id).model;
                const glm::mat4& parentWorld = scene.get<WorldTransform>(h.parent).model;
                worldPos              = glm::vec3(selfWorld[3]);
                worldRot              = worldRotationOf(selfWorld);
                frame.parented        = true;
                frame.parentWorldInv  = glm::inverse(parentWorld);
                frame.parentRot       = worldRotationOf(parentWorld);
            }
        }

        const uint32_t bodyIndex = static_cast<uint32_t>(m_bodies.size());
        m_bodies.push_back(id);
        bodyFrames.push_back(frame);

        PhysicsBody pb;
        pb.position = worldPos;
        pb.linearVelocity = rb.linearVelocity;
        pb.angularVelocity = rb.angularVelocity;
        // Sleeping or immovable bodies contribute infinite mass to the solver.
        const bool frozen = rb.sleeping || rb.inverseMass == 0.0f;
        pb.invMass = frozen ? 0.0f : rb.inverseMass;
        pb.invInertiaWorld = frozen ? glm::mat3(0.0f)
                                    : inertiaWorld(rb.invInertiaLocal, worldRot);
        pb.restitution = rb.restitution;
        pb.friction = rb.friction;
        m_solverBodies.push_back(pb);

        if (collider) {
            ColliderProxy proxy;
            proxy.body = bodyIndex;
            proxy.collider = *collider;
            proxy.position = worldPos;
            proxy.rotation = worldRot;
            proxy.cullStatic = rb.isStatic || rb.isKinematic;
            computeAABB(*collider, worldPos, worldRot, proxy.aabbMin, proxy.aabbMax);
            proxies.push_back(proxy);
        }
    }

    if (m_bodies.empty()) return;

    // Integrate forces -> velocities (skip sleeping / immovable).
    for (size_t k = 0; k < m_bodies.size(); ++k) {
        Rigidbody& rb = scene.get<Rigidbody>(m_bodies[k]);
        PhysicsBody& pb = m_solverBodies[k];
        if (rb.sleeping || rb.isStatic || rb.isKinematic || rb.inverseMass == 0.0f) continue;

        pb.linearVelocity += world.gravity * rb.gravityScale * dt;
        pb.linearVelocity *= 1.0f / (1.0f + rb.linearDamping * dt);
        pb.angularVelocity *= 1.0f / (1.0f + rb.angularDamping * dt);
    }

    // Broadphase: sort-and-sweep on the X axis.
    thread_local std::vector<uint32_t> sorted;
    thread_local std::vector<std::pair<uint32_t, uint32_t>> pairs;  // proxy index pairs
    sorted.clear();
    pairs.clear();

    for (uint32_t p = 0; p < proxies.size(); ++p) sorted.push_back(p);

    std::sort(sorted.begin(), sorted.end(), [&](uint32_t a, uint32_t b) {
        return proxies[a].aabbMin.x < proxies[b].aabbMin.x;
    });

    for (size_t a = 0; a < sorted.size(); ++a) {
        const ColliderProxy& pa = proxies[sorted[a]];
        for (size_t b = a + 1; b < sorted.size(); ++b) {
            const ColliderProxy& pb = proxies[sorted[b]];
            if (pb.aabbMin.x > pa.aabbMax.x) break;  // sorted: no further overlap on X
            if (pa.cullStatic && pb.cullStatic) continue;
            if (aabbOverlap(pa, pb)) pairs.emplace_back(sorted[a], sorted[b]);
        }
    }

    // Narrowphase: generate contact manifolds.
    m_manifolds.clear();
    std::vector<bool> hasContact(m_bodies.size(), false);
    Contact scratch[MAX_CONTACTS_PER_MANIFOLD];

    // Colliders expand into their child boxes here, so a single body pair can
    // yield several manifolds (one per child box-pair that touches). The solver
    // already handles many manifolds per body pair, so this just works - each
    // manifold carries the same bodyA/bodyB indices.
    thread_local std::vector<SubShape> subA;
    thread_local std::vector<SubShape> subB;

    for (const auto& pair : pairs) {
        const ColliderProxy& A = proxies[pair.first];
        const ColliderProxy& B = proxies[pair.second];
        const bool trigger = A.collider.isTrigger || B.collider.isTrigger;

        expandSubShapes(A, subA);
        expandSubShapes(B, subB);

        bool anyContact = false;
        glm::vec3 contactPoint(0.0f);
        glm::vec3 contactNormal(0.0f, 1.0f, 0.0f);
        for (const SubShape& sa : subA) {
            for (const SubShape& sb : subB) {
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
            hasContact[A.body] = true;
            hasContact[B.body] = true;

            // Surface the overlap to gameplay (enqueued: listeners fire on the
            // next EventSystem flush, never mid-solve). Triggers are queried,
            // not resolved, so they only produce events.
            const EntityId entityA = m_bodies[A.body];
            const EntityId entityB = m_bodies[B.body];
            if (trigger) {
                if (A.collider.isTrigger) m_events.enqueue(TriggerEvent{entityA, entityB});
                if (B.collider.isTrigger) m_events.enqueue(TriggerEvent{entityB, entityA});
            } else {
                m_events.enqueue(CollisionEvent{entityA, entityB, contactPoint, contactNormal});
            }
        }
    }

    // Wake sleepers struck by a faster body before solving.
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
            m_solverBodies[idx].invMass = dynamicInverseMass(rb);
            m_solverBodies[idx].invInertiaWorld =
                inertiaWorld(rb.invInertiaLocal, scene.get<Transform>(m_bodies[idx]).rotation);
        };
        if (rbA.sleeping && !rbB.sleeping && speedB > WAKE_SPEED_SQ) wake(a, rbA);
        if (rbB.sleeping && !rbA.sleeping && speedA > WAKE_SPEED_SQ) wake(b, rbB);
    }

    // Solve contacts (sequential impulse + split-impulse correction).
    SolverParams params;
    params.iterations = world.solverIterations;
    params.dt = dt;
    solveContacts(m_solverBodies, m_manifolds, params);

    // Integrate velocities -> pose, write back, update sleep, mark dirty.
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
        const BodyFrame& frame = bodyFrames[k];
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

        // Sleep bookkeeping: rest while supported long enough, otherwise stay awake.
        if (!rb.isKinematic) {
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
