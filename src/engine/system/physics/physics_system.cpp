#include "system/physics/physics_system.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/clock.h"
#include "debug/engine_error_log.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/environment.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/hierarchy_operations.h"
#include "core/event/event_bus.h"
#include "net/net_session.h"
#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "platform/threading/thread_pool.h"
#include "system/physics/authoring/mesh_collider.h"
#include "system/physics/body_pose.h"
#include "system/physics/inertia.h"
#include "system/physics/physics_events.h"
#include "system/physics/ragdoll_system.h"
#include "system/physics/collision/mesh_bvh.h"
#include "system/physics/collision/narrowphase.h"
#include "core/math/bounds.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Fastest any point of a body may move and still count as resting, m/s.
 *
 * Centre speed plus spin times reach, since a toppling tall box turns slowly while its top moves fast.
 * Box2D's figure.
 */
constexpr float SLEEP_SPEED = 0.05f;
constexpr float SLEEP_DELAY = 0.5f;  ///< Seconds of rest before sleeping.

/// No body this tick holds that slot.
constexpr uint32_t NO_BODY = ~0u;

float horizontalLengthSq(const glm::vec3& normal) {
    return normal.x * normal.x + normal.z * normal.z;
}

// Zero for anything but a Dynamic body of positive mass: nothing the solver does moves it.
float inverseMassOf(const Rigidbody& rb, RigidbodyMotion motion) {
    return motion == RigidbodyMotion::Dynamic && rb.mass > 0.0f ? 1.0f / rb.mass : 0.0f;
}

template <typename Fn>
void forEachMeshPart(const ColliderProxy& proxy, Fn&& fn) {
    for (const ColliderPart& part : proxy.collider->parts) {
        if (part.shape == ColliderShape::Mesh) fn(*proxy.collider, part);
    }
}

// One part's bound in its own frame, about its centre. A mesh's is its tree's root.
Math::AABB partLocalBounds(const Collider& collider, const ColliderPart& part) {
    switch (part.shape) {
        case ColliderShape::Box:
            return {-part.halfExtents, part.halfExtents};

        case ColliderShape::Capsule: {
            const glm::vec3 reach(part.radius, part.halfHeight + part.radius, part.radius);
            return {-reach, reach};
        }

        case ColliderShape::Mesh:
            // Over the same points the narrowphase places about the part's centre.
            if (collider.meshNodes.empty()) return {glm::vec3(0.0f), glm::vec3(0.0f)};
            return {collider.meshNodes[0].min, collider.meshNodes[0].max};

        case ColliderShape::Count:
            break;
    }
    return {-part.halfExtents, part.halfExtents};
}

// Every part's bound in the body's own frame, together. Empty collider: a point.
Math::AABB colliderLocalBounds(const Collider& collider) {
    if (collider.parts.empty()) return {glm::vec3(0.0f), glm::vec3(0.0f)};

    Math::AABB out{
        glm::vec3(std::numeric_limits<float>::max()),
        glm::vec3(std::numeric_limits<float>::lowest())
    };
    for (const ColliderPart& part : collider.parts) {
        const Math::AABB local = partLocalBounds(collider, part);
        out.min = glm::min(out.min, part.center + local.min);
        out.max = glm::max(out.max, part.center + local.max);
    }
    return out;
}

glm::mat3 localInverseInertia(const Rigidbody& rb, float invMass, const Collider* collider) {
    // freezeRotation: infinite rotational inertia, so contacts never torque it.
    if (rb.freezeRotation) return glm::mat3(0.0f);
    if (invMass == 0.0f || !collider || collider->parts.empty()) return glm::mat3(0.0f);

    glm::vec3 center(0.0f);
    glm::mat3 inertia(0.0f);
    if (collider->parts.size() == 1 && collider->parts[0].shape == ColliderShape::Capsule) {
        // The box approximation below is badly wrong for it; see docs/reference/physics.md.
        const ColliderPart& part = collider->parts[0];
        center  = part.center;
        inertia = capsuleInertiaLocal(rb.mass, part.radius, part.halfHeight);
    } else {
        // A solid box of the collider's overall local extent.
        const Math::AABB bounds = colliderLocalBounds(*collider);
        center  = (bounds.max + bounds.min) * 0.5f;
        inertia = boxInertiaLocal(rb.mass, (bounds.max - bounds.min) * 0.5f);
    }

    // Shifted to the entity origin, where the solver measures contact arms.
    if (inertia == glm::mat3(0.0f)) return glm::mat3(0.0f);
    const glm::mat3 shifted = glm::dot(center, center) > 0.0f
        ? parallelAxisShift(inertia, rb.mass, center)
        : inertia;
    return glm::inverse(shifted);
}

bool aabbOverlap(const Math::AABB& a, const Math::AABB& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x
        && a.min.y <= b.max.y && a.max.y >= b.min.y
        && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

/**
 * @brief Place one proxy's parts in world space, each with its world bound.
 *
 * Appends each box and capsule, with its bound, to its shape's arrays and records the spans on the
 * proxy, whose bound is their union. See ColliderProxy.
 *
 * @param p Proxy whose collider is placed.
 * @param position The body's world position.
 * @param rotation The body's world orientation.
 * @param boxes Every proxy's boxes, appended to.
 * @param boxBounds World bound of each box, parallel to @p boxes.
 * @param capsules Every proxy's capsules, appended to.
 * @param capsuleBounds World bound of each capsule, parallel to @p capsules.
 */
void expandSubShapes(
    ColliderProxy& p,
    const glm::vec3& position,
    const glm::quat& rotation,
    std::vector<BoxShape>& boxes,
    std::vector<Math::AABB>& boxBounds,
    std::vector<CapsuleShape>& capsules,
    std::vector<Math::AABB>& capsuleBounds
) {
    p.boxFirst     = static_cast<uint32_t>(boxes.size());
    p.capsuleFirst = static_cast<uint32_t>(capsules.size());
    const Math::AABB emptyBound{
        glm::vec3(std::numeric_limits<float>::max()),
        glm::vec3(std::numeric_limits<float>::lowest())
    };
    p.bounds = p.collider->parts.empty() ? Math::AABB{position, position} : emptyBound;
    const auto enclose = [&](const Math::AABB& part) {
        p.bounds.min = glm::min(p.bounds.min, part.min);
        p.bounds.max = glm::max(p.bounds.max, part.max);
    };

    const glm::mat3 r = glm::mat3_cast(rotation);

    for (const ColliderPart& part : p.collider->parts) {
        const glm::vec3 center = position + r * part.center;

        switch (part.shape) {
            case ColliderShape::Capsule: {
                // The capsule's segment runs along the body's local +Y.
                const glm::vec3 axis = r[1] * part.halfHeight;
                const glm::vec3 a = center - axis;
                const glm::vec3 b = center + axis;
                capsules.push_back({a, b, part.radius});
                const glm::vec3 grow(part.radius);
                capsuleBounds.push_back({glm::min(a, b) - grow, glm::max(a, b) + grow});
                enclose(capsuleBounds.back());
                break;
            }
            case ColliderShape::Mesh: {
                // See ColliderProxy: only its tree's root bound, placed like a box, joins the proxy's.
                const Math::AABB local = partLocalBounds(*p.collider, part);
                const glm::vec3 middle = (local.min + local.max) * 0.5f;
                const glm::vec3 half   = (local.max - local.min) * 0.5f;
                const glm::vec3 at     = center + r * middle;
                const glm::vec3 reach  = glm::abs(r[0]) * half.x
                    + glm::abs(r[1]) * half.y
                    + glm::abs(r[2]) * half.z;
                enclose({at - reach, at + reach});
                break;
            }
            case ColliderShape::Box: {
                boxes.push_back({center, {r[0], r[1], r[2]}, part.halfExtents});
                const glm::vec3 reach = glm::abs(r[0]) * part.halfExtents.x
                    + glm::abs(r[1]) * part.halfExtents.y
                    + glm::abs(r[2]) * part.halfExtents.z;
                boxBounds.push_back({center - reach, center + reach});
                enclose(boxBounds.back());
                break;
            }

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

    // A replacement world reuses the slots the cache keys by, so it would seed contacts never made.
    // Its touching pairs go unreported: their ids now name the new world's entities.
    if (scene.epoch() != m_worldEpoch) {
        m_contactCache.clear();
        m_jointImpulses.clear();
        m_touchingLastTick.clear();
        m_worldEpoch = scene.epoch();
    }

    const PhysicsSettings& physics = scene.physics();
    const bool replaying = ctx.net.replaying();

    if (!gatherBodies(scene, ctx.resources, ctx.net)) {
        // Nothing touches, so whatever touched on the last live tick has ended.
        if (!replaying) reportContacts(scene, ctx.events);
        return;
    }

    // Before the narrowphase, which skips contacts between jointed bodies.
    gatherJoints(scene);

    broadphase();

    narrowphase(replaying);

    buildIslands();

    // After the islands wake what was struck, or a body woken this tick would hang for it.
    integrateForces(physics, dt);

    solve(physics, dt);

    writeback(scene, replaying, dt);

    // After writeback, which decides what sleeps.
    if (!replaying) reportContacts(scene, ctx.events);

    // After the solve: reads this tick's contacts, so the island applies from the next tick; a contact's
    // first tick still pushes an immovable body.
    leaseContacts(scene, ctx.net);
}

bool PhysicsSystem::gatherBodies(Scene& scene, const ResourceManager& resources, const NetSession& net) {
    PROFILE_SCOPE("Physics/Gather");

    auto* rbStorage = scene.storage<Rigidbody>();
    if (!rbStorage) {
        m_massless.endPass();
        return false;
    }

    m_bodies.clear();
    m_solverBodies.clear();
    m_proxies.clear();
    m_shapeBoxes.clear();
    m_shapeBoxBounds.clear();
    m_shapeCapsules.clear();
    m_shapeCapsuleBounds.clear();
    m_bodyFrames.clear();

    markPosedByAnimation(scene, m_posedBone);

    const uint32_t rbCount = static_cast<uint32_t>(rbStorage->size());
    for (uint32_t i = 0; i < rbCount; ++i) {
        const uint32_t idx = rbStorage->keyAt(i);
        const EntityId id = scene.entityAt(idx);
        const Transform* local = scene.tryGet<Transform>(id);
        if (!local) continue;

        Rigidbody& rb = rbStorage->dataAt(i);
        Collider* collider = scene.tryGet<Collider>(id);
        if (collider) syncMeshCollider(*collider, resources);

        if (rb.motion == RigidbodyMotion::Dynamic && rb.mass <= 0.0f && m_massless.report()) {
            const Name* name = scene.tryGet<Name>(id);
            reportError("Physics", name ? name->value : "a body", "a Dynamic body needs a positive mass");
        }

        // A bone an inactive ragdoll poses is a hitbox, placed by the animation: kinematic, with no contacts.
        const bool posed = idx < m_posedBone.size() && m_posedBone[idx];

        BodyFrame frame;
        frame.rb = &rb;
        frame.motion = posed ? RigidbodyMotion::Kinematic : rb.motion;
        // Re-derived every tick, so edits to mass or collider need no "apply".
        frame.invMass = inverseMassOf(rb, frame.motion);
        frame.invInertiaLocal = localInverseInertia(rb, frame.invMass, collider);
        if (collider) {
            // The bound's farthest corner from the origin.
            const Math::AABB bounds = colliderLocalBounds(*collider);
            frame.reach = glm::length(glm::max(glm::abs(bounds.min), glm::abs(bounds.max)));
        }

        frame.pose    = worldPoseOf(scene, id, *local);
        frame.decided = net.simulates(id);
        const glm::vec3 worldPos = frame.pose.position;
        const glm::quat worldRot = frame.pose.rotation;

        const uint32_t bodyIndex = static_cast<uint32_t>(m_bodies.size());
        m_bodies.push_back(id);
        m_bodyFrames.push_back(frame);

        PhysicsBody pb;
        pb.position = worldPos;
        pb.rotation = worldRot;
        pb.linearVelocity = rb.linearVelocity;
        // A rotation-frozen body sheds residual spin, so its orientation stays script-owned.
        pb.angularVelocity = rb.freezeRotation ? glm::vec3(0.0f) : rb.angularVelocity;
        // Asleep, immovable, or another end's to move.
        const bool frozen = rb.sleeping || frame.invMass == 0.0f || !frame.decided;
        pb.invMass = frozen ? 0.0f : frame.invMass;
        pb.invInertiaWorld = frozen ? glm::mat3(0.0f) : inverseInertiaWorld(frame.invInertiaLocal, worldRot);
        pb.restitution = rb.restitution;
        pb.friction = rb.friction;
        m_solverBodies.push_back(pb);

        if (collider && collider->enabled && !posed) {
            ColliderProxy proxy;
            proxy.body = bodyIndex;
            proxy.collider = collider;
            proxy.isTrigger = collider->isTrigger;
            // In a replay only bodies this end decides can move.
            proxy.immovable = frame.invMass == 0.0f || (net.replaying() && !frame.decided);
            proxy.fixed     = frame.motion == RigidbodyMotion::Static;
            proxy.layer = rb.layer;
            proxy.collidesWith = rb.collidesWith;
            expandSubShapes(
                proxy,
                worldPos,
                worldRot,
                m_shapeBoxes,
                m_shapeBoxBounds,
                m_shapeCapsules,
                m_shapeCapsuleBounds
            );
            m_proxies.push_back(proxy);
        }
    }

    m_contacts.assign(m_bodies.size(), BodyContacts{});
    m_massless.endPass();

    return !m_bodies.empty();
}

void PhysicsSystem::integrateForces(const PhysicsSettings& physics, float dt) {
    PROFILE_SCOPE("Physics/Integrate");

    for (size_t k = 0; k < m_bodies.size(); ++k) {
        PhysicsBody& pb = m_solverBodies[k];
        // Zero for undecided bodies too: given gravity, one would press on its support with speed the solver
        // cannot take back.
        if (pb.invMass == 0.0f) continue;

        const Rigidbody& rb = *m_bodyFrames[k].rb;

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

    // Ties break on slot, so the order is a function of the world, not of add/remove history.
    std::sort(m_sorted.begin(), m_sorted.end(), [&](uint32_t a, uint32_t b) {
        const ColliderProxy& pa = m_proxies[a];
        const ColliderProxy& pb = m_proxies[b];
        if (pa.bounds.min.x != pb.bounds.min.x) return pa.bounds.min.x < pb.bounds.min.x;
        return m_bodies[pa.body].slot() < m_bodies[pb.body].slot();
    });

    for (size_t a = 0; a < m_sorted.size(); ++a) {
        const ColliderProxy& pa = m_proxies[m_sorted[a]];
        for (size_t b = a + 1; b < m_sorted.size(); ++b) {
            const ColliderProxy& pb = m_proxies[m_sorted[b]];
            if (pb.bounds.min.x > pa.bounds.max.x) break;  // sorted on X: no further overlap
            // Neither end can move, so nothing resolves - unless a trigger is asking.
            if (pa.immovable && pb.immovable && !pa.isTrigger && !pb.isTrigger) continue;
            // A trigger never senses the level, or it would report the floor under it every tick.
            if ((pa.isTrigger && pb.fixed) || (pb.isTrigger && pa.fixed)) continue;

            // Both ways round, so the answer cannot depend on which was asked.
            if ((pa.layer & pb.collidesWith) == 0) continue;
            if ((pb.layer & pa.collidesWith) == 0) continue;
            if (!aabbOverlap(pa.bounds, pb.bounds)) continue;

            // A is the lower slot, not whichever sorted first (equal bounds swap by rounding): A decides
            // the normal's direction, the cache key and the event's order.
            const bool swapped = m_bodies[pb.body].slot() < m_bodies[pa.body].slot();
            m_pairs.emplace_back(swapped ? m_sorted[b] : m_sorted[a], swapped ? m_sorted[a] : m_sorted[b]);
        }
    }
}

void PhysicsSystem::narrowphase(bool replaying) {
    PROFILE_SCOPE("Physics/Narrowphase");

    m_manifolds.clear();

    const size_t pairCount  = m_pairs.size();
    const size_t chunkCount = (pairCount + NARROWPHASE_CHUNK_PAIRS - 1) / NARROWPHASE_CHUNK_PAIRS;
    if (m_narrowphaseChunks.size() < chunkCount) m_narrowphaseChunks.resize(chunkCount);

    {
        PROFILE_SCOPE("Physics/Narrowphase/Pairs");
        // Each chunk writes only its own NarrowphaseChunk, so threading does not change the answer.
        parallelFor(chunkCount, 1, [&](size_t c) {
            NarrowphaseChunk& chunk = m_narrowphaseChunks[c];
            chunk.touching.clear();
            chunk.manifolds.clear();
            const size_t end = std::min(pairCount, (c + 1) * NARROWPHASE_CHUNK_PAIRS);
            for (size_t p = c * NARROWPHASE_CHUNK_PAIRS; p < end; ++p) {
                collidePair(static_cast<uint32_t>(p), chunk);
            }
        });
    }

    PROFILE_SCOPE("Physics/Narrowphase/Merge");
    for (size_t c = 0; c < chunkCount; ++c) {
        const NarrowphaseChunk& chunk = m_narrowphaseChunks[c];
        const ContactManifold* manifold = chunk.manifolds.data();

        for (const TouchingPair& touch : chunk.touching) {
            const ColliderProxy& A = m_proxies[m_pairs[touch.pair].first];
            const ColliderProxy& B = m_proxies[m_pairs[touch.pair].second];
            if (!replaying) {
                TrackedPair tracked;
                tracked.a        = m_bodies[A.body];
                tracked.b        = m_bodies[B.body];
                tracked.aTrigger = A.isTrigger;
                tracked.bTrigger = B.isTrigger;
                tracked.point    = touch.point;
                tracked.normal   = touch.normal;
                m_touchingThisTick.push_back(tracked);
            }

            if (A.isTrigger || B.isTrigger) continue;

            for (uint32_t m = 0; m < touch.manifoldCount; ++m, ++manifold) {
                m_manifolds.push_back(*manifold);

                // The most upward normal each body is held by: A's surface pushes along -normal, B's +normal.
                for (int k = 0; k < manifold->count; ++k) {
                    const glm::vec3& normal = manifold->contacts[k].normal;
                    if (-normal.y > m_contacts[A.body].support.y) m_contacts[A.body].support = -normal;
                    if ( normal.y > m_contacts[B.body].support.y) m_contacts[B.body].support =  normal;

                    // The normals differ only in sign, so one measure serves both.
                    const float horizontal = horizontalLengthSq(normal);
                    if (horizontal > horizontalLengthSq(m_contacts[A.body].block))
                        m_contacts[A.body].block = -normal;
                    if (horizontal > horizontalLengthSq(m_contacts[B.body].block))
                        m_contacts[B.body].block =  normal;
                }
            }

            // Only a resolved contact counts: a trigger holds nothing up.
            m_contacts[A.body].touched = true;
            m_contacts[B.body].touched = true;
        }
    }
}

void PhysicsSystem::reportContacts(const Scene& scene, EventBus& events) {
    PROFILE_SCOPE("Physics/Events");

    // Nothing about the contact changes until woken. A kinematic body is driven, so never rests.
    const auto rests = [&](EntityId entity) {
        const Rigidbody& rb = scene.get<Rigidbody>(entity);
        return rb.sleeping || rb.motion == RigidbodyMotion::Static;
    };

    const auto report = [&](const TrackedPair& pair, ContactPhase phase) {
        if (!pair.aTrigger && !pair.bTrigger) {
            CollisionEvent event{pair.a, pair.b, phase};
            if (phase != ContactPhase::Ended) {
                event.point  = pair.point;
                event.normal = pair.normal;
            }
            events.enqueue(event);
            return;
        }
        if (pair.aTrigger) events.enqueue(TriggerEvent{pair.a, pair.b, phase});
        if (pair.bTrigger) events.enqueue(TriggerEvent{pair.b, pair.a, phase});
    };

    std::vector<TrackedPair>& now  = m_touchingThisTick;
    std::vector<TrackedPair>& last = m_touchingLastTick;
    std::sort(now.begin(), now.end(), TrackedPair::before);

    // One merge walk over two sorted lists, so the three phases come out
    // interleaved in TrackedPair::before order.
    size_t n = 0;
    size_t l = 0;
    while (n < now.size() || l < last.size()) {
        if (l == last.size() || (n < now.size() && TrackedPair::before(now[n], last[l]))) {
            report(now[n++], ContactPhase::Began);
        } else if (n == now.size() || TrackedPair::before(last[l], now[n])) {
            // Its entities may be dead; the survivor was told it began.
            report(last[l++], ContactPhase::Ended);
        } else {
            const TrackedPair& pair = now[n++];
            ++l;
            if (!rests(pair.a) || !rests(pair.b)) report(pair, ContactPhase::Stayed);
        }
    }

    last.swap(now);
    now.clear();
}

void PhysicsSystem::collidePair(uint32_t pair, NarrowphaseChunk& chunk) const {
    const ColliderProxy& A = m_proxies[m_pairs[pair].first];
    const ColliderProxy& B = m_proxies[m_pairs[pair].second];

    if (!m_jointedPairs.empty()) {
        const uint64_t low  = glm::min(A.body, B.body);
        const uint64_t high = glm::max(A.body, B.body);
        if (std::binary_search(m_jointedPairs.begin(), m_jointedPairs.end(), (low << 32) | high)) {
            return;
        }
    }

    // Placed in world space at gather.
    const BoxShape*     boxesA         = m_shapeBoxes.data() + A.boxFirst;
    const BoxShape*     boxesB         = m_shapeBoxes.data() + B.boxFirst;
    const CapsuleShape* capsulesA      = m_shapeCapsules.data() + A.capsuleFirst;
    const CapsuleShape* capsulesB      = m_shapeCapsules.data() + B.capsuleFirst;
    const Math::AABB*   boxBoundsA     = m_shapeBoxBounds.data() + A.boxFirst;
    const Math::AABB*   boxBoundsB     = m_shapeBoxBounds.data() + B.boxFirst;
    const Math::AABB*   capsuleBoundsA = m_shapeCapsuleBounds.data() + A.capsuleFirst;
    const Math::AABB*   capsuleBoundsB = m_shapeCapsuleBounds.data() + B.capsuleFirst;
    const uint32_t boxCountA     = A.boxCount;
    const uint32_t boxCountB     = B.boxCount;
    const uint32_t capsuleCountA = A.capsuleCount;
    const uint32_t capsuleCountB = B.capsuleCount;

    const bool trigger = A.isTrigger || B.isTrigger;

    Contact scratch[MAX_CONTACTS_PER_MANIFOLD];
    TouchingPair touch;
    touch.pair = pair;
    bool anyContact = false;
    // A trigger's first contact answers it; the remaining shapes are never tried.
    bool sensed = false;

    // A body pair can yield several manifolds, all with the same bodyA/bodyB; contacts arrive A -> B.
    auto record = [&](int n) {
        if (n == 0) return;
        if (!anyContact) {  // keep the first contact as the event's representative
            touch.point  = scratch[0].point;
            touch.normal = scratch[0].normal;
        }
        anyContact = true;
        if (trigger) {  // queried, not resolved
            sensed = true;
            return;
        }

        ContactManifold manifold;
        manifold.bodyA = A.body;
        manifold.bodyB = B.body;
        manifold.count = std::min(n, MAX_CONTACTS_PER_MANIFOLD);
        for (int c = 0; c < manifold.count; ++c) manifold.contacts[c] = scratch[c];
        chunk.manifolds.push_back(manifold);
        ++touch.manifoldCount;
    };

    for (uint32_t i = 0; i < boxCountA && !sensed; ++i) {
        if (!aabbOverlap(boxBoundsA[i], B.bounds)) continue;
        for (uint32_t j = 0; j < boxCountB && !sensed; ++j) {
            if (!aabbOverlap(boxBoundsA[i], boxBoundsB[j])) continue;
            record(contactBoxes(boxesA[i], boxesB[j], scratch));
        }
    }

    for (uint32_t i = 0; i < capsuleCountA && !sensed; ++i) {
        if (!aabbOverlap(capsuleBoundsA[i], B.bounds)) continue;
        for (uint32_t j = 0; j < boxCountB && !sensed; ++j) {
            if (!aabbOverlap(capsuleBoundsA[i], boxBoundsB[j])) continue;
            record(contactCapsuleBox(capsulesA[i], boxesB[j], scratch));
        }
    }

    // Capsule-vs-box is not symmetric: run capsule-first, then flip the normals back to A -> B.
    for (uint32_t i = 0; i < capsuleCountB && !sensed; ++i) {
        if (!aabbOverlap(capsuleBoundsB[i], A.bounds)) continue;
        for (uint32_t j = 0; j < boxCountA && !sensed; ++j) {
            if (!aabbOverlap(capsuleBoundsB[i], boxBoundsA[j])) continue;
            const int n = contactCapsuleBox(capsulesB[i], boxesA[j], scratch);
            for (int c = 0; c < n; ++c) scratch[c].normal = -scratch[c].normal;
            record(n);
        }
    }

    for (uint32_t i = 0; i < capsuleCountA && !sensed; ++i) {
        if (!aabbOverlap(capsuleBoundsA[i], B.bounds)) continue;
        for (uint32_t j = 0; j < capsuleCountB && !sensed; ++j) {
            if (!aabbOverlap(capsuleBoundsA[i], capsuleBoundsB[j])) continue;
            record(contactCapsuleCapsule(capsulesA[i], capsulesB[j], scratch));
        }
    }

    // Only triangles under the other shape are fetched; a triangle is convex, so one routine covers all.
    const auto meshAgainst = [&](
        const ColliderProxy& meshProxy,
        const Collider& meshCollider,
        const ColliderPart& meshPart,
        const SupportShape& other,
        const Math::AABB& otherBounds,
        bool meshIsA
    ) {
        const PhysicsBody& meshBody = m_solverBodies[meshProxy.body];
        const glm::mat3 rotation = glm::mat3_cast(meshBody.rotation);
        const glm::mat3 toLocal = glm::transpose(rotation);
        // The tree is over the raw points: the bound comes back through the body's pose and the part's
        // centre, both, or the slab test misses.
        const glm::vec3 origin = meshBody.position + rotation * meshPart.center;
        const glm::vec3 corners[2] = {otherBounds.min - origin, otherBounds.max - origin};
        glm::vec3 localMin(std::numeric_limits<float>::max());
        glm::vec3 localMax(std::numeric_limits<float>::lowest());
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner = {
                corners[(i >> 0) & 1].x,
                corners[(i >> 1) & 1].y,
                corners[(i >> 2) & 1].z
            };
            const glm::vec3 local = toLocal * corner;
            localMin = glm::min(localMin, local);
            localMax = glm::max(localMax, local);
        }

        chunk.meshCandidates.clear();
        queryMeshBvh(meshCollider.meshNodes, localMin, localMax, chunk.meshCandidates);

        for (uint32_t triangle : chunk.meshCandidates) {
            if (sensed) break;
            const uint32_t base = triangle * 3;
            if (base + 2 >= meshCollider.meshPoints.size()) continue;

            glm::vec3 world[3];
            for (int c = 0; c < 3; ++c) {
                world[c] = origin + rotation * meshCollider.meshPoints[base + c];
            }

            // Normals run from the face to the shape: A -> B only when the mesh is A.
            const int n = contactTriangle(world, other, scratch);
            if (!meshIsA) {
                for (int c = 0; c < n; ++c) scratch[c].normal = -scratch[c].normal;
            }
            record(n);
        }
    };

    forEachMeshPart(A, [&](const Collider& c, const ColliderPart& p) {
        for (uint32_t i = 0; i < boxCountB && !sensed; ++i)
            meshAgainst(A, c, p, supportOf(boxesB[i]), boxBoundsB[i], true);
        for (uint32_t i = 0; i < capsuleCountB && !sensed; ++i)
            meshAgainst(A, c, p, supportOf(capsulesB[i]), capsuleBoundsB[i], true);
    });
    forEachMeshPart(B, [&](const Collider& c, const ColliderPart& p) {
        for (uint32_t i = 0; i < boxCountA && !sensed; ++i)
            meshAgainst(B, c, p, supportOf(boxesA[i]), boxBoundsA[i], false);
        for (uint32_t i = 0; i < capsuleCountA && !sensed; ++i)
            meshAgainst(B, c, p, supportOf(capsulesA[i]), capsuleBoundsA[i], false);
    });

    if (anyContact) chunk.touching.push_back(touch);
}

bool PhysicsSystem::isIslandMember(uint32_t body) const {
    return body < m_bodies.size() && m_bodyFrames[body].invMass > 0.0f && m_bodyFrames[body].decided;
}

void PhysicsSystem::buildIslands() {
    PROFILE_SCOPE("Physics/Islands");

    const uint32_t count = static_cast<uint32_t>(m_bodies.size());

    // Statics never join, or the floor would put the whole level in one island. Joints join as contacts
    // do, since a jointed pair makes no manifold.
    m_islands.reset(count);
    const auto join = [&](uint32_t a, uint32_t b) {
        if (isIslandMember(a) && isIslandMember(b)) m_islands.join(a, b);
    };
    for (const ContactManifold& manifold : m_manifolds) join(manifold.bodyA, manifold.bodyB);
    for (const JointConstraint& joint : m_joints) join(joint.bodyA, joint.bodyB);

    // Held: a contact or joint to a non-member (a lamp's chain holds it to its post). Pushed: by an awake
    // undecided body or a driven kinematic one, never a static, or the floor would wake everything.
    const auto pushes = [&](uint32_t body) {
        if (body >= count) return false;
        const BodyFrame& frame = m_bodyFrames[body];
        if (frame.rb->sleeping) return false;
        if (frame.invMass > 0.0f) return true;
        if (frame.motion != RigidbodyMotion::Kinematic) return false;
        const PhysicsBody& moving = m_solverBodies[body];
        return glm::dot(moving.linearVelocity, moving.linearVelocity) > 0.0f
            || glm::dot(moving.angularVelocity, moving.angularVelocity) > 0.0f;
    };
    m_islandHeld.assign(count, 0);
    m_islandDisturbed.assign(count, 0);
    const auto touch = [&](uint32_t member, uint32_t other) {
        if (!isIslandMember(member) || isIslandMember(other)) return;
        const uint32_t root = m_islands.find(member);
        m_islandHeld[root] = 1;
        if (pushes(other)) m_islandDisturbed[root] = 1;
    };
    for (const ContactManifold& manifold : m_manifolds) {
        touch(manifold.bodyA, manifold.bodyB);
        touch(manifold.bodyB, manifold.bodyA);
    }
    for (const JointConstraint& joint : m_joints) {
        touch(joint.bodyA, joint.bodyB);
        touch(joint.bodyB, joint.bodyA);
    }

    // And from inside: one member awake moves the rest.
    for (uint32_t k = 0; k < count; ++k) {
        if (!isIslandMember(k)) continue;
        if (!m_bodyFrames[k].rb->sleeping) m_islandDisturbed[m_islands.find(k)] = 1;
    }

    for (uint32_t k = 0; k < count; ++k) {
        if (!isIslandMember(k)) continue;
        Rigidbody& rb = *m_bodyFrames[k].rb;
        if (!rb.sleeping) continue;

        // Held by nothing: what it slept on is gone, and frozen it would hang in the air.
        const uint32_t root = m_islands.find(k);
        if (!m_islandDisturbed[root] && m_islandHeld[root]) continue;

        Rigidbody::wake(rb);
        m_solverBodies[k].invMass = m_bodyFrames[k].invMass;
        // The gathered world rotation, not the local Transform's, which differs for a parented body.
        m_solverBodies[k].invInertiaWorld =
            inverseInertiaWorld(m_bodyFrames[k].invInertiaLocal, m_solverBodies[k].rotation);
    }
}

void PhysicsSystem::gatherJoints(Scene& scene) {
    PROFILE_SCOPE("Physics/GatherJoints");

    m_joints.clear();
    m_jointedPairs.clear();

    auto* storage = scene.storage<Joint>();
    if (!storage) return;

    // From the bodies gathered, so an entity not simulated this tick resolves to NO_BODY.
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
        // Checked as a handle, not a slot: a dead entity's slot is reused.
        if (!joint.connected || !scene.isAlive(joint.connected)) continue;
        // A body held to itself is held to nothing.
        if (joint.connected == self) continue;

        const uint32_t bodyA = bodyIndexOf(self.slot());
        if (bodyA == NO_BODY) continue;

        const uint32_t connected = bodyIndexOf(joint.connected.slot());
        uint32_t bodyB = 0;
        glm::mat3 rotB(1.0f);
        if (connected != NO_BODY) {
            bodyB = connected;
            rotB = glm::mat3_cast(m_solverBodies[bodyB].rotation);
        } else if (scene.has<Transform>(joint.connected)) {
            // A pose but no body: the joint holds to that world point.
            const glm::mat4 world = HierarchyOperations::computeWorldMatrix(scene, joint.connected);
            PhysicsBody anchor;
            anchor.position = glm::vec3(world[3]);
            anchor.rotation = Math::worldRotationOf(world);
            bodyB = static_cast<uint32_t>(m_solverBodies.size());
            m_solverBodies.push_back(anchor);
            rotB = glm::mat3_cast(anchor.rotation);
        } else {
            continue;
        }

        const glm::mat3 rotA = glm::mat3_cast(m_solverBodies[bodyA].rotation);

        JointConstraint constraint;
        constraint.bodyA = bodyA;
        constraint.bodyB = bodyB;
        constraint.anchorA = rotA * joint.anchor;
        constraint.anchorB = rotB * joint.connectedAnchor;
        constraint.stiffness = glm::clamp(joint.stiffness, 0.0f, 1.0f);

        // The angle a hold keeps is the pair's when they last began to move: a ragdoll keeps the pose it
        // went live in, not the one it was built in.
        const PhysicsBody& heldA = m_solverBodies[bodyA];
        const PhysicsBody& heldB = m_solverBodies[bodyB];
        if (heldA.invMass + heldB.invMass <= 0.0f) {
            joint.heldResolved = false;
        } else if (!joint.heldResolved) {
            joint.heldRotation = glm::conjugate(heldA.rotation) * heldB.rotation;
            joint.heldResolved = true;
        }
        constraint.holdTorque   = glm::max(joint.holdTorque, 0.0f);
        constraint.heldRotation = joint.heldRotation;

        if (joint.type == JointType::Distance) {
            // Unset: whatever the two were apart when the joint first ran.
            if (joint.distance >= 0.0f) {
                joint.resolvedDistance = joint.distance;
            } else if (joint.resolvedDistance < 0.0f) {
                const glm::vec3 worldA = m_solverBodies[bodyA].position + constraint.anchorA;
                const glm::vec3 worldB = m_solverBodies[bodyB].position + constraint.anchorB;
                joint.resolvedDistance = glm::length(worldB - worldA);
            }
            constraint.distance = joint.resolvedDistance;
        }

        if (!joint.collideConnected) {
            const uint64_t low  = glm::min(constraint.bodyA, constraint.bodyB);
            const uint64_t high = glm::max(constraint.bodyA, constraint.bodyB);
            m_jointedPairs.push_back((low << 32) | high);
        }

        m_joints.push_back(constraint);
    }

    // Sorted for the narrowphase's binary search.
    std::sort(m_jointedPairs.begin(), m_jointedPairs.end());

    // Gauss-Seidel makes order part of the answer: sorted by slots, not Joint insertion order. A world
    // anchor's synthetic body has no slot.
    const auto slotOf = [&](uint32_t body) {
        return body < m_bodies.size() ? m_bodies[body].slot() : std::numeric_limits<uint32_t>::max();
    };
    std::sort(m_joints.begin(), m_joints.end(), [&](const JointConstraint& a, const JointConstraint& b) {
        const uint32_t slotA = slotOf(a.bodyA);
        const uint32_t slotB = slotOf(b.bodyA);
        if (slotA != slotB) return slotA < slotB;
        return slotOf(a.bodyB) < slotOf(b.bodyB);
    });

    // An entity carries one Joint, so its body (always A) names the joint; both lists run in slot order.
    size_t held = 0;
    for (JointConstraint& constraint : m_joints) {
        const EntityId owner = m_bodies[constraint.bodyA];
        while (held < m_jointImpulses.size() && m_jointImpulses[held].owner.slot() < owner.slot()) {
            ++held;
        }
        if (held < m_jointImpulses.size() && m_jointImpulses[held].owner == owner) {
            constraint.impulse     = m_jointImpulses[held].impulse;
            constraint.holdImpulse = m_jointImpulses[held].holdImpulse;
        }
    }
}

void PhysicsSystem::solve(const PhysicsSettings& physics, float dt) {
    PROFILE_SCOPE("Physics/Solve");
    PROFILE_PLOT("Physics/Bodies",    static_cast<int64_t>(m_bodies.size()));
    PROFILE_PLOT("Physics/Manifolds", static_cast<int64_t>(m_manifolds.size()));
    {
        int64_t contacts = 0;
        for (const ContactManifold& manifold : m_manifolds) contacts += manifold.count;
        PROFILE_PLOT("Physics/Contacts", contacts);
    }

    SolverParams params;
    params.iterations = physics.solverIterations;
    params.dt = dt;

    // What a pair needed last tick is what it needs now.
    m_contactCache.seed(m_manifolds, m_bodies, m_bodyFrames);
    solveStep(m_solverBodies, m_manifolds, m_joints, params);
    m_contactCache.record(m_manifolds, m_bodies, m_bodyFrames);

    m_jointImpulses.clear();
    for (const JointConstraint& joint : m_joints) {
        m_jointImpulses.push_back({m_bodies[joint.bodyA], joint.impulse, joint.holdImpulse});
    }
}

void PhysicsSystem::leaseContacts(Scene& scene, NetSession& net) {
    PROFILE_SCOPE("Physics/Lease");

    if (net.role() != NetRole::Client) return;

    const EntityId owner = net.localEntity();
    if (!owner || !scene.isAlive(owner)) return;

    uint32_t ownerBody = UINT32_MAX;
    for (uint32_t i = 0; i < m_bodies.size(); ++i) {
        if (m_bodies[i] == owner) {
            ownerBody = i;
            break;
        }
    }
    if (ownerBody == UINT32_MAX) return;

    // Through no immovable body and onto no other player's character; see docs/reference/networking.md,
    // "Leasing".
    const uint32_t count = static_cast<uint32_t>(m_bodies.size());
    m_leaseCarries.assign(count, 0);
    for (uint32_t k = 0; k < count; ++k) {
        m_leaseCarries[k] = k == ownerBody
            || (m_bodyFrames[k].invMass > 0.0f && !scene.has<CharacterController>(m_bodies[k]));
    }

    m_leaseIslands.reset(count);
    const auto join = [&](uint32_t a, uint32_t b) {
        if (a < count && b < count && m_leaseCarries[a] && m_leaseCarries[b]) m_leaseIslands.join(a, b);
    };
    for (const ContactManifold& manifold : m_manifolds) join(manifold.bodyA, manifold.bodyB);
    for (const JointConstraint& joint : m_joints) join(joint.bodyA, joint.bodyB);

    m_leased.clear();
    const uint32_t island = m_leaseIslands.find(ownerBody);
    for (uint32_t k = 0; k < count; ++k) {
        if (k != ownerBody && m_leaseIslands.find(k) == island) m_leased.push_back(m_bodies[k]);
    }

    net.lease(m_leased);
}

void PhysicsSystem::writeback(Scene& scene, bool replaying, float dt) {
    PROFILE_SCOPE("Physics/Writeback");

    for (size_t k = 0; k < m_bodies.size(); ++k) {
        const EntityId id = m_bodies[k];
        Rigidbody& rb = *m_bodyFrames[k].rb;
        PhysicsBody& pb = m_solverBodies[k];

        // Before the early-outs: a sleeping body on the floor is supported, and a controller asks then.
        const bool decided = m_bodyFrames[k].decided;
        if (decided || !replaying) {
            rb.supported     = m_contacts[k].touched;
            rb.supportNormal = rb.supported ? m_contacts[k].support : Math::WORLD_UP;
            rb.blockNormal   = rb.supported ? m_contacts[k].block   : Math::WORLD_UP;
        }
        if (!decided) continue;

        // The solver did not move a body with no inverse mass: nothing to write back.
        if (m_bodyFrames[k].invMass == 0.0f) continue;
        if (rb.sleeping) {
            // Whether its support is still there is its island's question.
            rb.linearVelocity = glm::vec3(0.0f);
            rb.angularVelocity = glm::vec3(0.0f);
            continue;
        }

        rb.linearVelocity = pb.linearVelocity;
        rb.angularVelocity = pb.angularVelocity;

        // The solve integrated the pose; this only maps it home.
        Transform& t = scene.get<Transform>(id);
        const BodyFrame& frame = m_bodyFrames[k];
        if (frame.pose.parented) {
            t.position = glm::vec3(glm::inverse(frame.pose.parentWorld) * glm::vec4(pb.position, 1.0f));
            t.rotation = glm::normalize(glm::conjugate(frame.pose.parentRot) * pb.rotation);
        } else {
            t.position = pb.position;
            t.rotation = pb.rotation;
        }

        // canSleep false opts out: a dozing script-driven character has its velocity writes zeroed.
        // Whether the timer may act is sleepIslands' answer. A replayed tick was counted when lived.
        if (rb.canSleep && !replaying) {
            const float pointSpeed = glm::length(rb.linearVelocity)
                + frame.reach * glm::length(rb.angularVelocity);
            const bool resting = pointSpeed < SLEEP_SPEED;
            if (resting) rb.sleepTimer += dt;
            else         rb.sleepTimer  = 0.0f;
        }
    }

    sleepIslands(replaying);
}

void PhysicsSystem::sleepIslands(bool replaying) {
    PROFILE_SCOPE("Physics/Sleep");

    // A live-timeline decision.
    if (replaying) return;

    // Islands are wholly asleep or awake. One unready member keeps its island awake: a crate asleep on
    // a settling stack is immovable, and the stack slides out.
    const uint32_t count = static_cast<uint32_t>(m_bodies.size());
    m_islandReady.assign(count, 1);
    for (uint32_t k = 0; k < count; ++k) {
        if (!isIslandMember(k)) continue;
        const Rigidbody& rb = *m_bodyFrames[k].rb;
        if (rb.sleeping) continue;
        if (!rb.canSleep || rb.sleepTimer < SLEEP_DELAY) m_islandReady[m_islands.find(k)] = 0;
    }

    for (uint32_t k = 0; k < count; ++k) {
        if (!isIslandMember(k)) continue;
        Rigidbody& rb = *m_bodyFrames[k].rb;
        const uint32_t root = m_islands.find(k);
        if (rb.sleeping || !m_islandHeld[root] || !m_islandReady[root]) continue;

        rb.sleeping        = true;
        rb.linearVelocity  = glm::vec3(0.0f);
        rb.angularVelocity = glm::vec3(0.0f);
    }
}

} // namespace Vkm::Engine
