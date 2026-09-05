#include "system/physics/physics_system.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/clock.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/environment.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/rigidbody.h"
#include "core/event/event_bus.h"
#include "net/net_session.h"
#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "system/physics/body_pose.h"
#include "system/physics/inertia.h"
#include "system/physics/physics_events.h"
#include "system/physics/collision/gjk.h"
#include "system/physics/tolerance.h"
#include "system/physics/collision/mesh_bvh.h"
#include "system/physics/collision/narrowphase.h"
#include "core/math/bounds.h"

namespace Vkm::Engine {

namespace {

// Sleep thresholds: a body must rest (low speed while in contact) for this long
// before it stops simulating; a resting body wakes if struck by a faster one.
constexpr float SLEEP_LINEAR_SQ  = 0.04f;   // (0.2 m/s)^2
constexpr float SLEEP_ANGULAR_SQ = 0.04f;   // (0.2 rad/s)^2
constexpr float SLEEP_DELAY      = 0.5f;    // seconds of rest before sleeping
constexpr float WAKE_SPEED_SQ    = 0.25f;   // partner speed^2 that wakes a sleeper


// How much of a normal points sideways. Squared, because it is only ever
// compared against another of its own kind.
float horizontalLengthSq(const glm::vec3& normal) {
    return normal.x * normal.x + normal.z * normal.z;
}

float dynamicInverseMass(const Rigidbody& rb) {
    if (Rigidbody::isImmovable(rb)) return 0.0f;
    return 1.0f / rb.mass;
}

/**
 * @brief Disjoint sets over the tick's bodies, for grouping what rests together.
 *
 * Path-halving find and union by size, which is enough for a few thousand bodies
 * and a few thousand pairs once a tick. Rebuilt each tick rather than kept:
 * contacts are a per-tick fact and an island that outlived them would be a
 * second, staler answer to the same question.
 */
class BodyIslands {
    public:
        explicit BodyIslands(size_t count) : m_parent(count), m_size(count, 1) {
            for (size_t i = 0; i < count; ++i) m_parent[i] = static_cast<uint32_t>(i);
        }

    public:
        uint32_t find(uint32_t i) {
            while (m_parent[i] != i) {
                m_parent[i] = m_parent[m_parent[i]];
                i = m_parent[i];
            }
            return i;
        }

        void join(uint32_t a, uint32_t b) {
            a = find(a);
            b = find(b);
            if (a == b) return;
            if (m_size[a] < m_size[b]) std::swap(a, b);
            m_parent[b] = a;
            m_size[a]  += m_size[b];
        }

    private:
        std::vector<uint32_t> m_parent;
        std::vector<uint32_t> m_size;
};

// A body the integrator and solver leave alone: asleep or immovable (static,
// kinematic, or non-positive mass). Takes the tick's inverse mass rather than
// re-deriving it - dynamicInverseMass already folds static/kinematic/zero-mass
// into the value gatherBodies parked on the body frame.
bool isFrozen(const Rigidbody& rb, float invMass) {
    return rb.sleeping || invMass == 0.0f;
}

// Every mesh part of one proxy. A collider may hold several parts and only
// some are meshes, and the caller wants the component beside each because that
// is where the triangles and the tree live.
template <typename Fn>
void forEachMeshPart(const ColliderProxy& proxy,
                     const std::vector<ColliderPart>& parts, Fn&& fn) {
    if (!proxy.collider) return;
    for (uint32_t i = 0; i < proxy.partsCount; ++i) {
        const ColliderPart& part = parts[proxy.partsFirst + i];
        if (part.shape == ColliderShape::Mesh) fn(*proxy.collider, part);
    }
}

// World-space bound of one shape, for asking a mesh which triangles are near it.
void shapeBounds(const SupportShape& shape, glm::vec3& min, glm::vec3& max) {
    const glm::vec3 axes[3] = {
        {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}
    };
    for (int i = 0; i < 3; ++i) {
        min[i] = support(shape, -axes[i])[i];
        max[i] = support(shape,  axes[i])[i];
    }
}

// Half-extent of one part's own local-space AABB. A capsule's is its radius on
// the two axes across the segment and radius + halfHeight along it; a mesh's is
// whatever its points reach, halfExtents saying nothing about it. Both read the
// same buffer, so both take the same branch - a mesh falling through to
// halfExtents gave a sixteen-metre floor a half-metre bound, and bodies landed
// on it only where they happened to be near its origin.
glm::vec3 partLocalExtent(const Collider& collider, const ColliderPart& part) {
    switch (part.shape) {
        case ColliderShape::Box:
            return part.halfExtents;

        case ColliderShape::Capsule:
            return glm::vec3(part.radius, part.halfHeight + part.radius,
                             part.radius);

        case ColliderShape::Mesh: {
            glm::vec3 extent(0.0f);
            for (uint32_t i = 0; i < part.meshCount; ++i) {
                const uint32_t index = part.meshFirst + i;
                if (index >= collider.meshPoints.size()) break;
                extent = glm::max(extent, glm::abs(collider.meshPoints[index]));
            }
            return extent;
        }

        case ColliderShape::Count:
            break;
    }
    return part.halfExtents;
}

glm::mat3 localInverseInertia(const Rigidbody& rb, const Collider* collider) {
    // freezeRotation: infinite rotational inertia. Contact impulses then apply
    // zero torque in the solver, so the body translates but never tumbles -
    // the character-controller case (a runner shouldn't spin from a graze).
    if (rb.freezeRotation) return glm::mat3(0.0f);
    if (dynamicInverseMass(rb) == 0.0f || !collider || collider->parts.empty())
        return glm::mat3(0.0f);

    glm::vec3 center(0.0f);
    glm::mat3 inertia(0.0f);
    if (collider->parts.size() == 1 && collider->parts[0].shape == ColliderShape::Capsule) {
        // The one shape the box approximation below is badly wrong about.
        // See docs/reference/system/physics.md.
        const ColliderPart& part = collider->parts[0];
        center  = part.center;
        inertia = capsuleInertiaLocal(rb.mass, part.radius, part.halfHeight);
    } else {
        // Approximate the collider's inertia with a solid box of its overall
        // local extent - exact per-part inertia isn't worth it for gameplay.
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(std::numeric_limits<float>::lowest());
        for (const ColliderPart& part : collider->parts) {
            const glm::vec3 extent = partLocalExtent(*collider, part);
            mn = glm::min(mn, part.center - extent);
            mx = glm::max(mx, part.center + extent);
        }
        center  = (mx + mn) * 0.5f;
        inertia = boxInertiaLocal(rb.mass, (mx - mn) * 0.5f);
    }

    // Inertia about the collider centre, parallel-axis-shifted to the entity
    // origin (where the solver measures contact arms) so an off-centre collider
    // gets its rotational response about the correct axis.
    if (inertia == glm::mat3(0.0f)) return glm::mat3(0.0f);
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
    if (collider.parts.empty()) { outMin = pos; outMax = pos; return; }
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());
    const glm::mat3 r = glm::mat3_cast(rot);
    glm::mat4 model = glm::mat4_cast(rot);  // rotation block is constant per body
    for (const ColliderPart& part : collider.parts) {
        const glm::vec3 center = pos + r * part.center;
        glm::vec3 mn, mx;
        if (part.shape == ColliderShape::Capsule) {
            // Exact, and cheaper than the box path: the bound of a swept segment
            // is the bound of its endpoints grown by the radius.
            const glm::vec3 axis = r[1] * part.halfHeight;
            mn = glm::min(center - axis, center + axis) - glm::vec3(part.radius);
            mx = glm::max(center - axis, center + axis) + glm::vec3(part.radius);
        } else {
            // A mesh's bound is its points'; a box's is its half-extents. Both
            // are then the same problem: an oriented box put into world space.
            const glm::vec3 extent = partLocalExtent(collider, part);
            model[3] = glm::vec4(center, 1.0f);
            const Math::AABB world = Math::transform(model, {-extent, extent});
            mn = world.min;
            mx = world.max;
        }
        outMin = glm::min(outMin, mn);
        outMax = glm::max(outMax, mx);
    }
}

bool aabbOverlap(const ColliderProxy& a, const ColliderProxy& b) {
    return a.aabbMin.x <= b.aabbMax.x && a.aabbMax.x >= b.aabbMin.x
        && a.aabbMin.y <= b.aabbMax.y && a.aabbMax.y >= b.aabbMin.y
        && a.aabbMin.z <= b.aabbMax.z && a.aabbMax.z >= b.aabbMin.z;
}

// Place one proxy's parts in world space, appending each into the shared array
// for its shape and recording the two spans on the proxy. Called once per body
// at gather; see ColliderProxy for why the arrays are split by shape and why a
// mesh part is not one of them.
void expandSubShapes(ColliderProxy& p, const std::vector<ColliderPart>& parts,
                     std::vector<BoxShape>& boxes,
                     std::vector<CapsuleShape>& capsules) {
    p.boxFirst     = static_cast<uint32_t>(boxes.size());
    p.capsuleFirst = static_cast<uint32_t>(capsules.size());

    const glm::mat3 r = glm::mat3_cast(p.rotation);

    for (uint32_t i = 0; i < p.partsCount; ++i) {
        const ColliderPart& part = parts[p.partsFirst + i];
        const glm::vec3 center = p.position + r * part.center;

        switch (part.shape) {
            case ColliderShape::Capsule: {
                // The capsule's segment runs along the body's local +Y.
                const glm::vec3 axis = r[1] * part.halfHeight;
                capsules.push_back({center - axis, center + axis, part.radius});
                break;
            }
            case ColliderShape::Mesh:
                break;  // see ColliderProxy: walked per pair, not expanded
            case ColliderShape::Box:
                boxes.push_back({center, {r[0], r[1], r[2]}, part.halfExtents});
                break;

            case ColliderShape::Count:
                break;
        }
    }

    p.boxCount     = static_cast<uint32_t>(boxes.size())    - p.boxFirst;
    p.capsuleCount = static_cast<uint32_t>(capsules.size()) - p.capsuleFirst;
}

} // namespace

void PhysicsSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("PhysicsSystem");

    Scene& scene = ctx.scene;
    const float dt = ctx.clock.getFixedStep();

    // Scene-global like the Environment, but deliberately not part of it.
    const PhysicsSettings& physics = scene.physics();

    if (!gatherBodies(scene, ctx.net)) return;

    integrateForces(scene, physics, dt);

    // Before the pairing rather than before the solve: what a joint holds
    // together must not also be pushed apart, and the narrowphase is where
    // that is decided.
    gatherJoints(scene);

    broadphase();

    narrowphase(ctx.events, ctx.net.replaying());

    wakeConnected(scene);

    solve(physics, dt);

    writeback(scene, ctx.net.replaying(), dt);

    // After the solve, because it reads this tick's contacts - so the island
    // applies from the next tick and the first tick of a contact still pushes
    // an immovable body. The alternative is guessing before the narrowphase.
    leaseContacts(scene, ctx.net);
}

bool PhysicsSystem::gatherBodies(Scene& scene, const NetSession& net) {
    PROFILE_SCOPE("Physics/Gather");

    auto* rbStorage = scene.storage<Rigidbody>();
    if (!rbStorage) return false;

    m_bodies.clear();
    m_solverBodies.clear();
    m_proxies.clear();
    m_proxyParts.clear();   // capacity kept: the parts are POD, so no per-body allocation
    m_shapeBoxes.clear();
    m_shapeCapsules.clear();
    m_bodyFrames.clear();

    const uint32_t rbCount = static_cast<uint32_t>(rbStorage->size());
    for (uint32_t i = 0; i < rbCount; ++i) {
        const uint32_t idx = rbStorage->keyAt(i);
        const EntityId id = scene.entityAt(idx);
        if (!scene.has<Transform>(id)) continue;

        Rigidbody& rb = rbStorage->dataAt(i);
        const Transform& t = scene.get<Transform>(id);
        const Collider* collider = scene.tryGet<Collider>(id);

        BodyFrame frame;
        // Defensively re-derive mass properties so editor edits to mass/collider
        // take effect without an explicit "apply" step.
        frame.invMass = dynamicInverseMass(rb);
        frame.invInertiaLocal = localInverseInertia(rb, collider);

        const BodyPose pose = worldPoseOf(scene, id, t);
        const glm::vec3 worldPos = pose.position;
        const glm::quat worldRot = pose.rotation;
        frame.parented       = pose.parented;
        frame.parentWorld    = pose.parentWorld;
        frame.parentRot      = pose.parentRot;
        frame.worldRot       = worldRot;
        // A body this end does not decide is immovable here and still collides,
        // so a client's character stands on a crate and is stopped by one but
        // never moves one. Asked once, here, and read by every phase after.
        frame.decided        = net.simulates(id);

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
        const bool frozen = isFrozen(rb, frame.invMass) || !frame.decided;
        pb.invMass = frozen ? 0.0f : frame.invMass;
        pb.invInertiaWorld = frozen ? glm::mat3(0.0f)
                                    : inverseInertiaWorld(frame.invInertiaLocal, worldRot);
        pb.restitution = rb.restitution;
        pb.friction = rb.friction;
        m_solverBodies.push_back(pb);

        if (collider && collider->enabled) {
            ColliderProxy proxy;
            proxy.body = bodyIndex;
            proxy.collider = collider;
            proxy.partsFirst = static_cast<uint32_t>(m_proxyParts.size());
            proxy.partsCount = static_cast<uint32_t>(collider->parts.size());
            proxy.isTrigger  = collider->isTrigger;
            m_proxyParts.insert(m_proxyParts.end(),
                                collider->parts.begin(), collider->parts.end());
            proxy.position = worldPos;
            proxy.rotation = worldRot;
            // During a replay only bodies this end decides can move, so a pair
            // that cannot is worth never forming. Says so through the
            // broadphase's existing static-against-static skip, not a new rule.
            proxy.immovable = Rigidbody::isImmovable(rb)
                           || (net.replaying() && !frame.decided);
            proxy.layer = rb.layer;
            proxy.collidesWith = rb.collidesWith;
            computeAABB(*collider, worldPos, worldRot, proxy.aabbMin, proxy.aabbMax);
            // Once per body, not once per pair it turns out to be in.
            expandSubShapes(proxy, m_proxyParts, m_shapeBoxes, m_shapeCapsules);
            m_proxies.push_back(proxy);
        }
    }

    // Seeded fresh each tick; assign() keeps the capacity.
    m_contacts.assign(m_bodies.size(), BodyContacts{});

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

    // Ties break on the entity slot, so the sweep order is a function of the
    // world rather than of the Rigidbody set's add-and-remove order: a row of
    // identical crates settles the same after an unrelated body is destroyed.
    std::sort(m_sorted.begin(), m_sorted.end(), [&](uint32_t a, uint32_t b) {
        const ColliderProxy& pa = m_proxies[a];
        const ColliderProxy& pb = m_proxies[b];
        if (pa.aabbMin.x != pb.aabbMin.x) return pa.aabbMin.x < pb.aabbMin.x;
        return m_bodies[pa.body].slot() < m_bodies[pb.body].slot();
    });

    for (size_t a = 0; a < m_sorted.size(); ++a) {
        const ColliderProxy& pa = m_proxies[m_sorted[a]];
        for (size_t b = a + 1; b < m_sorted.size(); ++b) {
            const ColliderProxy& pb = m_proxies[m_sorted[b]];
            if (pb.aabbMin.x > pa.aabbMax.x) break;  // sorted on X: no further overlap
            // Neither end can move, so no contact resolves into anything -
            // unless one is asking rather than resolving. A trigger pair still
            // builds no manifold of its own: `record` returns early.
            if (pa.immovable && pb.immovable && !pa.isTrigger && !pb.isTrigger) continue;

            // Both ways round, so "does A hit B" cannot depend on which was
            // asked. A pair that fails this is not a pair.
            if ((pa.layer & pb.collidesWith) == 0) continue;
            if ((pb.layer & pa.collidesWith) == 0) continue;
            if (aabbOverlap(pa, pb)) m_pairs.emplace_back(m_sorted[a], m_sorted[b]);
        }
    }
}

void PhysicsSystem::narrowphase(EventBus& events, bool replaying) {
    PROFILE_SCOPE("Physics/Narrowphase");

    m_manifolds.clear();
    Contact scratch[MAX_CONTACTS_PER_MANIFOLD];

    // Parts expand into world space here, so one body pair can yield several
    // manifolds. They all carry the same bodyA/bodyB, which is what lets the
    // solver take them as they come.

    // The proxies' shapes were placed in world space once each, at gather.
    const auto boxesOf = [&](const ColliderProxy& p) {
        return std::pair{m_shapeBoxes.data() + p.boxFirst, p.boxCount};
    };
    const auto capsulesOf = [&](const ColliderProxy& p) {
        return std::pair{m_shapeCapsules.data() + p.capsuleFirst, p.capsuleCount};
    };

    for (const auto& pair : m_pairs) {
        const ColliderProxy& A = m_proxies[pair.first];
        const ColliderProxy& B = m_proxies[pair.second];
        const auto [boxesA, boxCountA] = boxesOf(A);
        const auto [boxesB, boxCountB] = boxesOf(B);
        const auto [capsulesA, capsuleCountA] = capsulesOf(A);
        const auto [capsulesB, capsuleCountB] = capsulesOf(B);

        if (!m_jointedPairs.empty()) {
            const uint64_t low  = glm::min(A.body, B.body);
            const uint64_t high = glm::max(A.body, B.body);
            if (m_jointedPairs.count((low << 32) | high)) continue;
        }

        const bool trigger = A.isTrigger || B.isTrigger;

        bool anyContact = false;
        glm::vec3 contactPoint(0.0f);
        glm::vec3 contactNormal(0.0f, 1.0f, 0.0f);

        // Every shape pairing lands here, so the manifold bookkeeping is written
        // once. Whatever produced the contacts has already oriented them A -> B.
        auto record = [&](int n) {
            if (n == 0) return;
            if (!anyContact) {  // keep the first contact as the event's representative
                contactPoint  = scratch[0].point;
                contactNormal = scratch[0].normal;
            }
            anyContact = true;
            if (trigger) return;  // queried, not resolved

            ContactManifold manifold;
            manifold.bodyA = A.body;
            manifold.bodyB = B.body;
            manifold.count = std::min(n, MAX_CONTACTS_PER_MANIFOLD);
            for (int c = 0; c < manifold.count; ++c) manifold.contacts[c] = scratch[c];
            m_manifolds.push_back(manifold);

            // The most upward normal each body is held by. A's surface normal
            // is -normal and B's is +normal, the normal running A -> B and a
            // surface pushing back along its own outward direction.
            for (int c = 0; c < manifold.count; ++c) {
                const glm::vec3& normal = manifold.contacts[c].normal;
                if (-normal.y > m_contacts[A.body].support.y) m_contacts[A.body].support = -normal;
                if ( normal.y > m_contacts[B.body].support.y) m_contacts[B.body].support =  normal;

                // Both bodies see the same horizontal magnitude - the two
                // normals differ only in sign - so one measure serves both.
                const float horizontal = horizontalLengthSq(normal);
                if (horizontal > horizontalLengthSq(m_contacts[A.body].block))
                    m_contacts[A.body].block = -normal;
                if (horizontal > horizontalLengthSq(m_contacts[B.body].block))
                    m_contacts[B.body].block =  normal;
            }
        };

        for (uint32_t i = 0; i < boxCountA; ++i)
            for (uint32_t j = 0; j < boxCountB; ++j)
                record(contactBoxes(boxesA[i], boxesB[j], scratch));

        for (uint32_t i = 0; i < capsuleCountA; ++i)
            for (uint32_t j = 0; j < boxCountB; ++j)
                record(contactCapsuleBox(capsulesA[i], boxesB[j], scratch));

        // B's capsules against A's boxes. Capsule-vs-box is not symmetric, so it
        // runs capsule-first and the normals are flipped back to A -> B here.
        for (uint32_t i = 0; i < capsuleCountB; ++i) {
            for (uint32_t j = 0; j < boxCountA; ++j) {
                const int n = contactCapsuleBox(capsulesB[i], boxesA[j], scratch);
                for (int c = 0; c < n; ++c) scratch[c].normal = -scratch[c].normal;
                record(n);
            }
        }

        for (uint32_t i = 0; i < capsuleCountA; ++i)
            for (uint32_t j = 0; j < capsuleCountB; ++j)
                record(contactCapsuleCapsule(capsulesA[i], capsulesB[j], scratch));

        // Anything against a mesh. Only the triangles under the other shape
        // are fetched, and a triangle is convex, so one support-driven routine
        // covers every shape it can meet rather than one routine per pair.
        const auto meshAgainst = [&](const ColliderProxy& meshProxy,
                                     const Collider& meshCollider,
                                     const ColliderPart& meshPart,
                                     const SupportShape& other, bool meshIsA) {
            glm::vec3 min(0.0f);
            glm::vec3 max(0.0f);
            shapeBounds(other, min, max);

            // The tree is in the mesh body's frame and the other shape is in
            // the world, so the query goes into the mesh's frame rather than
            // every triangle coming out of it.
            const glm::mat3 rotation = glm::mat3_cast(meshProxy.rotation);
            const glm::mat3 toLocal = glm::transpose(rotation);
            // The tree is built over the raw points, so the query bound comes
            // back to that space through both the body's pose and the part's
            // centre - one without the other offsets it and the slab test misses.
            const glm::vec3 origin =
                meshProxy.position + rotation * meshPart.center;
            const glm::vec3 corners[2] = {min - origin, max - origin};
            glm::vec3 localMin(std::numeric_limits<float>::max());
            glm::vec3 localMax(std::numeric_limits<float>::lowest());
            for (int i = 0; i < 8; ++i) {
                const glm::vec3 corner = {corners[(i >> 0) & 1].x,
                                          corners[(i >> 1) & 1].y,
                                          corners[(i >> 2) & 1].z};
                const glm::vec3 local = toLocal * corner;
                localMin = glm::min(localMin, local);
                localMax = glm::max(localMax, local);
            }

            m_meshCandidates.clear();
            queryMeshBvh(meshCollider.meshNodes, localMin, localMax,
                         m_meshCandidates);

            for (uint32_t triangle : m_meshCandidates) {
                const uint32_t base = meshPart.meshFirst + triangle * 3;
                if (base + 2 >= meshCollider.meshPoints.size()) continue;

                glm::vec3 world[3];
                for (int c = 0; c < 3; ++c) {
                    world[c] = origin + rotation * meshCollider.meshPoints[base + c];
                }
                const SupportShape face = supportOfPoints(world, 3);
                const SupportShape& first  = meshIsA ? face  : other;
                const SupportShape& second = meshIsA ? other : face;
                if (!gjkContact(first, second, scratch[0])) continue;

                // The triangle decides the direction, not EPA: a zero-thickness
                // hull is symmetric about its plane, so the shallowest way out
                // flips as the centre crosses it. The winding says which side.
                const glm::vec3 edge = glm::cross(world[1] - world[0],
                                                  world[2] - world[0]);
                const float edgeLenSq = glm::dot(edge, edge);
                if (edgeLenSq > Physics::DEGENERATE_SQ) {
                    const glm::vec3 faceNormal = edge / std::sqrt(edgeLenSq);

                    // How far the body reaches past the face, measured along the
                    // face - never along EPA's answer. The two agree while the
                    // body is outside, and it matters when they stop.
                    const glm::vec3 deepest = support(other, -faceNormal);
                    const float depth = glm::dot(world[0] - deepest, faceNormal);
                    if (depth <= 0.0f) continue;

                    // The contact normal runs A -> B, so it is the face's own
                    // direction when the mesh is A and the reverse when it is B.
                    scratch[0].normal = faceNormal * (meshIsA ? 1.0f : -1.0f);
                    scratch[0].penetration = depth;
                    // The point moves with them. A normal from the face and a
                    // point from EPA describe different contacts, and the lever
                    // arm between the two is a torque nothing asked for.
                    scratch[0].point = deepest + faceNormal * (depth * 0.5f);
                }
                record(1);
            }
        };

        forEachMeshPart(A, m_proxyParts, [&](const Collider& c,
                                            const ColliderPart& p) {
            for (uint32_t i = 0; i < boxCountB; ++i)     meshAgainst(A, c, p, supportOf(boxesB[i]), true);
            for (uint32_t i = 0; i < capsuleCountB; ++i) meshAgainst(A, c, p, supportOf(capsulesB[i]), true);
        });
        forEachMeshPart(B, m_proxyParts, [&](const Collider& c,
                                            const ColliderPart& p) {
            for (uint32_t i = 0; i < boxCountA; ++i)     meshAgainst(B, c, p, supportOf(boxesA[i]), false);
            for (uint32_t i = 0; i < capsuleCountA; ++i) meshAgainst(B, c, p, supportOf(capsulesA[i]), false);
        });
        if (anyContact) {
            // Enqueued, not emitted: listeners fire on the next EventBus flush,
            // never mid-solve.
            const EntityId entityA = m_bodies[A.body];
            const EntityId entityB = m_bodies[B.body];
            if (trigger) {
                // A replay re-runs a tick whose contacts were already
                // reported, and a trigger entered twice for one entry is a
                // behavior acting on something that did not happen.
                if (A.isTrigger && !replaying) events.enqueue(TriggerEvent{entityA, entityB});
                if (B.isTrigger && !replaying) events.enqueue(TriggerEvent{entityB, entityA});
            } else {
                // Only a resolved contact supports the sleep test: a trigger
                // holds nothing up, and a body asleep inside one would never
                // be woken - wakeConnected walks manifolds, which skip them.
                m_contacts[A.body].touched = true;
                m_contacts[B.body].touched = true;
                if (!replaying) {
                    events.enqueue(CollisionEvent{entityA, entityB, contactPoint, contactNormal});
                }
            }
        }
    }
}

void PhysicsSystem::wakeConnected(Scene& scene) {
    PROFILE_SCOPE("Physics/Wake");

    auto wake = [&](uint32_t idx, Rigidbody& rb) {
        // The gather froze it deliberately. Handing its mass back would let one
        // tick of contact move a body this end has no say over, which writeback
        // then refuses - and the character walks into a hole nobody can see.
        if (!m_bodyFrames[idx].decided) return;

        rb.sleeping = false;
        rb.sleepTimer = 0.0f;
        m_solverBodies[idx].invMass = m_bodyFrames[idx].invMass;
        // The gathered world rotation, not the local Transform's: for a
        // parented body those differ, and the solver is running in world.
        m_solverBodies[idx].invInertiaWorld =
            inverseInertiaWorld(m_bodyFrames[idx].invInertiaLocal, m_bodyFrames[idx].worldRot);
    };

    // One rule for both ways two bodies are connected: if one end is moving and
    // the other is asleep, the sleeper is about to be disturbed and has to be
    // awake to notice.
    auto rouse = [&](uint32_t a, uint32_t b) {
        // A world pin is a synthetic body appended past the gathered ones: it
        // has an index in the solver and no entity behind it, so there is
        // nothing to wake and nothing to read.
        if (a >= m_bodies.size() || b >= m_bodies.size()) return;
        Rigidbody& rbA = scene.get<Rigidbody>(m_bodies[a]);
        Rigidbody& rbB = scene.get<Rigidbody>(m_bodies[b]);
        const float speedA = glm::dot(m_solverBodies[a].linearVelocity,
                                      m_solverBodies[a].linearVelocity);
        const float speedB = glm::dot(m_solverBodies[b].linearVelocity,
                                      m_solverBodies[b].linearVelocity);
        if (rbA.sleeping && !rbB.sleeping && speedB > WAKE_SPEED_SQ) wake(a, rbA);
        if (rbB.sleeping && !rbA.sleeping && speedA > WAKE_SPEED_SQ) wake(b, rbB);
    };

    for (const ContactManifold& manifold : m_manifolds) {
        rouse(manifold.bodyA, manifold.bodyB);
    }

    // Joints too: a jointed pair generates no manifold on purpose, so pulling
    // one end while the other sleeps does nothing - the solver treats a sleeper
    // as immovable.
    for (const JointConstraint& joint : m_joints) {
        rouse(joint.bodyA, joint.bodyB);
    }
}

void PhysicsSystem::gatherJoints(Scene& scene) {
    PROFILE_SCOPE("Physics/GatherJoints");

    m_joints.clear();
    m_jointedPairs.clear();

    auto* storage = scene.storage<Joint>();
    if (!storage) return;

    // Slot -> this tick's body index. Built from the bodies actually gathered,
    // so a joint to something not simulated resolves to nothing and is dropped.
    uint32_t maxSlot = 0;
    for (const EntityId body : m_bodies) maxSlot = std::max(maxSlot, body.slot());
    m_bodyIndexBySlot.assign(static_cast<size_t>(maxSlot) + 1, NO_BODY);
    for (uint32_t i = 0; i < m_bodies.size(); ++i) m_bodyIndexBySlot[m_bodies[i].slot()] = i;

    const auto bodyIndexOf = [&](uint32_t slot) {
        return slot < m_bodyIndexBySlot.size() ? m_bodyIndexBySlot[slot] : NO_BODY;
    };

    const uint32_t count = static_cast<uint32_t>(storage->size());
    for (uint32_t i = 0; i < count; ++i) {
        const EntityId self = scene.entityAt(storage->keyAt(i));
        Joint& joint = storage->dataAt(i);
        // Checked as a handle, not a slot: a dead entity's slot is reused, and
        // a joint matched by index alone would quietly tie itself to whatever
        // moved in.
        if (!joint.connected || !scene.isAlive(joint.connected)) continue;

        const uint32_t bodyA = bodyIndexOf(self.slot());
        if (bodyA == NO_BODY) continue;

        const uint32_t connected = bodyIndexOf(joint.connected.slot());
        uint32_t bodyB = 0;
        glm::mat3 rotB(1.0f);
        if (connected != NO_BODY) {
            bodyB = connected;
            rotB = glm::mat3_cast(m_bodyFrames[bodyB].worldRot);
        } else if (scene.has<Transform>(joint.connected)) {
            // The world pin: connected has a pose but no body, so the joint
            // holds to that point. A synthetic static body carries it through the
            // solver, appended after the real ones so nothing writes it back.
            const glm::mat4 world =
                HierarchyOperations::computeWorldMatrix(scene, joint.connected);
            PhysicsBody anchor;
            anchor.position = glm::vec3(world[3]);
            bodyB = static_cast<uint32_t>(m_solverBodies.size());
            m_solverBodies.push_back(anchor);
            rotB = glm::mat3_cast(Math::worldRotationOf(world));
        } else {
            continue;
        }

        const glm::mat3 rotA = glm::mat3_cast(m_bodyFrames[bodyA].worldRot);

        JointConstraint constraint;
        constraint.bodyA = bodyA;
        constraint.bodyB = bodyB;
        constraint.anchorA = rotA * joint.anchor;
        constraint.anchorB = rotB * joint.connectedAnchor;
        constraint.stiffness = glm::clamp(joint.stiffness, 0.0f, 1.0f);

        if (joint.type == JointType::Distance) {
            // Unset: whatever the two were apart when the joint first ran. A
            // rope built at play time has the length it was built with, and
            // computing it is the solver's arithmetic, not the author's.
            if (joint.distance >= 0.0f) {
                joint.resolvedDistance = joint.distance;
            } else if (joint.resolvedDistance < 0.0f) {
                const glm::vec3 worldA =
                    m_solverBodies[bodyA].position + constraint.anchorA;
                const glm::vec3 worldB =
                    m_solverBodies[bodyB].position + constraint.anchorB;
                joint.resolvedDistance = glm::length(worldB - worldA);
            }
            constraint.distance = joint.resolvedDistance;
        }

        if (!joint.collideConnected) {
            const uint64_t low  = glm::min(constraint.bodyA, constraint.bodyB);
            const uint64_t high = glm::max(constraint.bodyA, constraint.bodyB);
            m_jointedPairs.insert((low << 32) | high);
        }

        m_joints.push_back(constraint);
    }

    // Gauss-Seidel, so the solve order is part of the answer: sorting on the
    // pair of slots ties it to the scene rather than to the Joint set's
    // insertion order. A world-anchored joint's synthetic body has no slot.
    const auto slotOf = [&](uint32_t body) {
        return body < m_bodies.size()
            ? m_bodies[body].slot()
            : std::numeric_limits<uint32_t>::max();
    };
    std::sort(m_joints.begin(), m_joints.end(),
              [&](const JointConstraint& a, const JointConstraint& b) {
        const uint32_t slotA = slotOf(a.bodyA);
        const uint32_t slotB = slotOf(b.bodyA);
        if (slotA != slotB) return slotA < slotB;
        return slotOf(a.bodyB) < slotOf(b.bodyB);
    });
}

void PhysicsSystem::solve(const PhysicsSettings& physics, float dt) {
    PROFILE_SCOPE("Physics/Solve");

    SolverParams params;
    params.iterations = physics.solverIterations;
    params.dt = dt;

    // Seeded before, recorded after: what a pair needed last tick is what it
    // needs now, and a solve that starts from it holds a stack instead of
    // rediscovering its own weight eight passes at a time.
    m_contactCache.seed(m_manifolds, m_bodies, m_solverBodies);
    solveContacts(m_solverBodies, m_manifolds, params);

    // After the contacts, not beside them. A joint resolved first would pull a
    // body into a surface the contact pass then pushes it out of, and the two
    // would trade the body back and forth for as long as both were unhappy.
    solveJoints(m_solverBodies, m_joints, params);

    // The poses move here rather than in writeback, because what separates an
    // overlapping pair is the recovery velocity being spent on distance - and
    // the relax pass below exists to take that velocity back once it has been.
    // Integrated in the world frame the solver ran in; writeback maps it home.
    for (size_t k = 0; k < m_bodies.size(); ++k) {
        PhysicsBody& body = m_solverBodies[k];
        if (body.invMass == 0.0f) continue;
        body.position += body.linearVelocity * dt;
    }

    relaxContacts(m_solverBodies, m_manifolds, params);
    m_contactCache.record(m_manifolds, m_bodies, m_solverBodies);
}

void PhysicsSystem::leaseContacts(Scene& scene, NetSession& net) {
    PROFILE_SCOPE("Physics/Lease");

    if (net.role() != NetRole::Client) return;

    const EntityId owner = net.localEntity();
    if (!owner || !scene.isAlive(owner)) return;

    uint32_t ownerBody = UINT32_MAX;
    for (uint32_t i = 0; i < m_bodies.size(); ++i) {
        if (m_bodies[i] == owner) { ownerBody = i; break; }
    }
    if (ownerBody == UINT32_MAX) return;

    // Breadth-first over contacts and joints alike. A jointed pair is skipped
    // before the shape tests, so without joints a shoved ragdoll would lease
    // one limb and leave the joint hauling against a body the solver froze.
    m_leased.clear();
    m_leaseFrontier.clear();
    m_leaseFrontier.push_back(ownerBody);
    m_leaseReached.assign(m_bodies.size(), false);
    m_leaseReached[ownerBody] = true;

    const auto visit = [&](uint32_t from, uint32_t to) {
        if (from >= m_leaseReached.size() || to >= m_leaseReached.size()) return;
        if (!m_leaseReached[from] || m_leaseReached[to]) return;

        // Never through an immovable body. A crate rests on the floor and the
        // floor touches everything in the level, so a closure that stepped
        // through statics would lease the whole world in two hops.
        if (m_bodyFrames[to].invMass == 0.0f) return;

        // Nor onto another player's character. That one is theirs to predict,
        // and predicting it here means guessing at input this end never saw.
        const EntityId entity = m_bodies[to];
        if (scene.isAlive(entity) && scene.has<CharacterController>(entity)) return;

        m_leaseReached[to] = true;
        m_leaseFrontier.push_back(to);
        m_leased.push_back(entity);
    };

    for (size_t head = 0; head < m_leaseFrontier.size(); ++head) {
        const uint32_t at = m_leaseFrontier[head];

        // Only outward from a body that can push: an immovable one is a wall,
        // and what rests against a wall is not part of what this character is
        // moving.
        if (at != ownerBody && m_bodyFrames[at].invMass == 0.0f) continue;

        for (const ContactManifold& manifold : m_manifolds) {
            if (manifold.bodyA == at) visit(at, manifold.bodyB);
            if (manifold.bodyB == at) visit(at, manifold.bodyA);
        }
        for (const JointConstraint& joint : m_joints) {
            if (joint.bodyA == at) visit(at, joint.bodyB);
            if (joint.bodyB == at) visit(at, joint.bodyA);
        }
    }

    net.lease(m_leased);
}

void PhysicsSystem::writeback(Scene& scene, bool replaying, float dt) {
    PROFILE_SCOPE("Physics/Writeback");

    for (size_t k = 0; k < m_bodies.size(); ++k) {
        const EntityId id = m_bodies[k];
        Rigidbody& rb = scene.get<Rigidbody>(id);
        PhysicsBody& pb = m_solverBodies[k];

        // Published before the early-outs below: a sleeping body resting on the
        // floor is supported, and that is when a controller asks. In a replay an
        // unpairable body keeps what it last said instead.
        const bool decided = m_bodyFrames[k].decided;
        if (decided || !replaying) {
            rb.supported     = m_contacts[k].touched;
            rb.supportNormal = rb.supported ? m_contacts[k].support : Math::WORLD_UP;
            rb.blockNormal   = rb.supported ? m_contacts[k].block   : Math::WORLD_UP;
        }
        if (!decided) continue;

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
        const glm::vec3 angular = pb.angularVelocity;

        // The position was integrated in the solve, between the pass that asks
        // for an overlap back and the pass that takes the asking velocity out
        // again. The rotation is integrated here, from the velocity that
        // survived both.
        const glm::vec3 worldPos = pb.position;
        const glm::quat worldRotOld = frame.parented ? frame.parentRot * t.rotation : t.rotation;
        const glm::quat spin(0.0f, angular.x, angular.y, angular.z);
        const glm::quat worldRot = glm::normalize(worldRotOld + 0.5f * spin * worldRotOld * dt);

        if (frame.parented) {
            t.position = glm::vec3(glm::inverse(frame.parentWorld) * glm::vec4(worldPos, 1.0f));
            t.rotation = glm::normalize(glm::conjugate(frame.parentRot) * worldRot);
        } else {
            t.position = worldPos;
            t.rotation = worldRot;
        }

        // canSleep opts a body out entirely - a script-driven character that
        // dozes off has its velocity writes zeroed. The timer is this body's
        // own; whether it may act on it is sleepIslands' answer.
        if (!rb.isKinematic && rb.canSleep) {
            const float linSq = glm::dot(rb.linearVelocity, rb.linearVelocity);
            const float angSq = glm::dot(rb.angularVelocity, rb.angularVelocity);
            const bool resting = m_contacts[k].touched && linSq < SLEEP_LINEAR_SQ
                                 && angSq < SLEEP_ANGULAR_SQ;
            if (resting) rb.sleepTimer += dt;
            else         rb.sleepTimer  = 0.0f;
        }
    }

    sleepIslands(scene, replaying);
}

void PhysicsSystem::sleepIslands(Scene& scene, bool replaying) {
    PROFILE_SCOPE("Physics/Sleep");

    // The unit is the island, not the body: everything that can push everything
    // else sleeps together or not at all. A crate that sleeps while the stack
    // under it settles is immovable to the solver, and the stack slides out.
    BodyIslands islands(m_solverBodies.size());

    // Only dynamic pairs join. A static floor is in contact with everything
    // resting on it, so letting statics carry the union would put every body in
    // the level into one island and nothing would ever sleep.
    for (const ContactManifold& manifold : m_manifolds) {
        if (m_solverBodies[manifold.bodyA].invMass == 0.0f) continue;
        if (m_solverBodies[manifold.bodyB].invMass == 0.0f) continue;
        islands.join(manifold.bodyA, manifold.bodyB);
    }
    // Joints too: a body hanging off another is held by it, and a sleeper on the
    // end of a swinging rope is a body simulated no further.
    for (const JointConstraint& joint : m_joints) {
        if (m_solverBodies[joint.bodyA].invMass == 0.0f) continue;
        if (m_solverBodies[joint.bodyB].invMass == 0.0f) continue;
        islands.join(joint.bodyA, joint.bodyB);
    }

    // Ready means every dynamic member has held still long enough. One member
    // that is awake, opted out, or simply younger than the delay keeps its whole
    // island awake, which is the point.
    std::vector<uint8_t> ready(m_solverBodies.size(), 1);
    for (size_t k = 0; k < m_bodies.size(); ++k) {
        if (!m_bodyFrames[k].decided) continue;
        const Rigidbody& rb = scene.get<Rigidbody>(m_bodies[k]);
        if (rb.isStatic || rb.isKinematic || m_solverBodies[k].invMass == 0.0f) continue;

        const bool memberReady = rb.canSleep && rb.sleepTimer >= SLEEP_DELAY;
        if (!memberReady) ready[islands.find(static_cast<uint32_t>(k))] = 0;
    }

    for (size_t k = 0; k < m_bodies.size(); ++k) {
        if (!m_bodyFrames[k].decided) continue;
        Rigidbody& rb = scene.get<Rigidbody>(m_bodies[k]);
        if (rb.isStatic || rb.isKinematic || !rb.canSleep) continue;
        if (m_solverBodies[k].invMass == 0.0f) continue;
        if (!ready[islands.find(static_cast<uint32_t>(k))]) continue;

        // A replay must not put a body to sleep: the tick is being re-run from a
        // server correction, and sleeping is a decision about the live timeline.
        if (replaying) continue;

        rb.sleeping        = true;
        rb.linearVelocity  = glm::vec3(0.0f);
        rb.angularVelocity = glm::vec3(0.0f);
    }
}

} // namespace Vkm::Engine
