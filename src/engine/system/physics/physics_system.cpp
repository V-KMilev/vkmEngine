#include "system/physics/physics_system.h"

#include <algorithm>
#include <cstdint>
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
#include "system/hierarchy/hierarchy_operations.h"
#include "system/physics/inertia.h"
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

/// Broadphase / narrowphase view of one collidable body, cached per tick.
/// `body` indexes the parallel solver-body array; `cullStatic` is true only for
/// permanently immovable bodies (static/kinematic) - a sleeping dynamic body is
/// NOT cullStatic, so it keeps generating contacts and stays supported.
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
    if (dynamicInverseMass(rb) == 0.0f || !collider) return glm::mat3(0.0f);
    switch (collider->shape) {
        case ColliderShape::Sphere: return sphereInverseInertiaLocal(rb.mass, collider->radius);
        case ColliderShape::Box:    return boxInverseInertiaLocal(rb.mass, collider->halfExtents);
        case ColliderShape::Plane:  return glm::mat3(0.0f);
    }
    return glm::mat3(0.0f);
}

void computeAABB(
    const Collider& collider,
    const glm::vec3& pos,
    const glm::quat& rot,
    glm::vec3& outMin,
    glm::vec3& outMax
) {
    if (collider.shape == ColliderShape::Sphere) {
        outMin = pos - glm::vec3(collider.radius);
        outMax = pos + glm::vec3(collider.radius);
        return;
    }
    // Box: transform the local AABB with Arvo's method.
    glm::mat4 model = glm::mat4_cast(rot);
    model[3] = glm::vec4(pos, 1.0f);
    localToWorldAABB(model, -collider.halfExtents, collider.halfExtents, outMin, outMax);
}

bool aabbOverlap(const ColliderProxy& a, const ColliderProxy& b) {
    return a.aabbMin.x <= b.aabbMax.x && a.aabbMax.x >= b.aabbMin.x
        && a.aabbMin.y <= b.aabbMax.y && a.aabbMax.y >= b.aabbMin.y
        && a.aabbMin.z <= b.aabbMax.z && a.aabbMax.z >= b.aabbMin.z;
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

    const uint32_t rbCount = static_cast<uint32_t>(rbStorage->size());
    for (uint32_t i = 0; i < rbCount; ++i) {
        const uint32_t idx = rbStorage->keyAt(i);
        const EntityId id{idx, scene.generationOf(idx)};
        if (!scene.has<Transform>(id)) continue;

        Rigidbody& rb = rbStorage->dataAt(i);
        const Transform& t = scene.get<Transform>(id);
        const Collider* collider = scene.has<Collider>(id) ? &scene.get<Collider>(id) : nullptr;

        // Defensively re-derive mass properties so editor edits to mass/shape
        // take effect without an explicit "apply" step.
        rb.inverseMass = dynamicInverseMass(rb);
        rb.invInertiaLocal = localInverseInertia(rb, collider);

        const uint32_t bodyIndex = static_cast<uint32_t>(m_bodies.size());
        m_bodies.push_back(id);

        PhysicsBody pb;
        pb.position = t.position;
        pb.linearVelocity = rb.linearVelocity;
        pb.angularVelocity = rb.angularVelocity;
        // Sleeping or immovable bodies contribute infinite mass to the solver.
        const bool frozen = rb.sleeping || rb.inverseMass == 0.0f;
        pb.invMass = frozen ? 0.0f : rb.inverseMass;
        pb.invInertiaWorld = frozen ? glm::mat3(0.0f)
                                    : inertiaWorld(rb.invInertiaLocal, t.rotation);
        pb.restitution = rb.restitution;
        pb.friction = rb.friction;
        m_solverBodies.push_back(pb);

        if (collider) {
            ColliderProxy proxy;
            proxy.body = bodyIndex;
            proxy.collider = *collider;
            proxy.position = t.position;
            proxy.rotation = t.rotation;
            proxy.cullStatic = rb.isStatic || rb.isKinematic;
            if (collider->shape != ColliderShape::Plane) {
                computeAABB(*collider, t.position, t.rotation, proxy.aabbMin, proxy.aabbMax);
            }
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

    // Broadphase: sort-and-sweep finite bodies on X; planes tested apart.
    thread_local std::vector<uint32_t> finite;
    thread_local std::vector<uint32_t> planes;
    thread_local std::vector<std::pair<uint32_t, uint32_t>> pairs;  // proxy index pairs
    finite.clear();
    planes.clear();
    pairs.clear();

    for (uint32_t p = 0; p < proxies.size(); ++p) {
        if (proxies[p].collider.shape == ColliderShape::Plane) planes.push_back(p);
        else finite.push_back(p);
    }

    std::sort(finite.begin(), finite.end(), [&](uint32_t a, uint32_t b) {
        return proxies[a].aabbMin.x < proxies[b].aabbMin.x;
    });

    for (size_t a = 0; a < finite.size(); ++a) {
        const ColliderProxy& pa = proxies[finite[a]];
        for (size_t b = a + 1; b < finite.size(); ++b) {
            const ColliderProxy& pb = proxies[finite[b]];
            if (pb.aabbMin.x > pa.aabbMax.x) break;  // sorted: no further overlap on X
            if (pa.cullStatic && pb.cullStatic) continue;
            if (aabbOverlap(pa, pb)) pairs.emplace_back(finite[a], finite[b]);
        }
        for (uint32_t pl : planes) {
            if (pa.cullStatic) continue;  // plane is always static
            pairs.emplace_back(finite[a], pl);  // A = finite, B = plane
        }
    }

    // Narrowphase: generate contact manifolds.
    m_manifolds.clear();
    std::vector<bool> hasContact(m_bodies.size(), false);
    Contact scratch[MAX_CONTACTS_PER_MANIFOLD];

    for (const auto& pair : pairs) {
        const ColliderProxy& A = proxies[pair.first];
        const ColliderProxy& B = proxies[pair.second];
        const int n = generateContacts(
            A.collider, A.position, A.rotation,
            B.collider, B.position, B.rotation,
            scratch);
        if (n == 0) continue;

        hasContact[A.body] = true;
        hasContact[B.body] = true;
        if (A.collider.isTrigger || B.collider.isTrigger) continue;  // queried, not resolved

        ContactManifold manifold;
        manifold.bodyA = A.body;
        manifold.bodyB = B.body;
        manifold.count = std::min(n, MAX_CONTACTS_PER_MANIFOLD);
        for (int c = 0; c < manifold.count; ++c) manifold.contacts[c] = scratch[c];
        m_manifolds.push_back(manifold);
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
        const glm::vec3 linear = pb.linearVelocity + pb.pseudoLinear;
        const glm::vec3 angular = pb.angularVelocity + pb.pseudoAngular;

        t.position = pb.position + linear * dt;
        const glm::quat spin(0.0f, angular.x, angular.y, angular.z);
        t.rotation = glm::normalize(t.rotation + 0.5f * spin * t.rotation * dt);

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
