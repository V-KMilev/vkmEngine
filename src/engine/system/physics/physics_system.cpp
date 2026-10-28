#include "system/physics/physics_system.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/clock.h"
#include "debug/debug_draw.h"
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
#include "ecs/component/render/terrain.h"
#include "ecs/hierarchy_operations.h"
#include "core/event/event_bus.h"
#include "net/net_session.h"
#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "platform/threading/thread_pool.h"
#include "resource/resource_manager.h"
#include "system/physics/authoring/mesh_collider.h"
#include "system/physics/body_pose.h"
#include "system/physics/mass_model.h"
#include "system/physics/query/query.h"
#include "system/physics/physics_events.h"
#include "system/physics/ragdoll_system.h"
#include "system/physics/collision/mesh_bvh.h"
#include "system/physics/collision/narrowphase.h"
#include "system/physics/collision/part_bounds.h"
#include "core/math/bounds.h"

namespace Vkm::Engine {

namespace {

/// m_proxyOf for a body with no proxy: no collider, a disabled one, or a bone a ragdoll poses.
constexpr uint32_t NO_PROXY = ~0u;

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

template <typename Fn>
void forEachMeshPart(const ColliderProxy& proxy, Fn&& fn) {
    for (const ColliderPart& part : proxy.collider->parts) {
        if (isTriangles(part.shape)) fn(*proxy.collider, part);
    }
}

glm::mat3 localInverseInertia(const Rigidbody& rb, float invMass, const Collider* collider) {
    // freezeRotation: infinite rotational inertia, so contacts never torque it.
    if (rb.freezeRotation) return glm::mat3(0.0f);
    if (invMass == 0.0f || !collider || collider->parts.empty()) return glm::mat3(0.0f);

    // About the centre of mass, where the solver measures contact arms.
    const glm::mat3 inertia = MassModel::of(collider).inertia(rb.mass);
    if (inertia == glm::mat3(0.0f)) return glm::mat3(0.0f);
    return glm::inverse(inertia);
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
    p.bounds = p.collider->parts.empty() ? Math::AABB{position, position} : Math::AABB::empty();
    const auto enclose = [&](const Math::AABB& part) { p.bounds.expand(part); };

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
            case ColliderShape::Mesh:
            case ColliderShape::Terrain: {
                // See ColliderProxy: only its tree's root bound, placed like a box, joins the proxy's.
                enclose(Math::transform(r, center, partLocalBounds(*p.collider, part)));
                break;
            }
            case ColliderShape::Box: {
                boxes.push_back({center, {r[0], r[1], r[2]}, part.halfExtents});
                boxBounds.push_back(Math::transform(r, center, {-part.halfExtents, part.halfExtents}));
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

/**
 * @brief How fast two bodies' points at @p point close along @p normal (a -> b), m/s; zero apart.
 *
 * Read before the solve, as gathered: how hard a contact struck, not what is left of it after.
 */
float closingSpeed(
    const PhysicsBody& a,
    const PhysicsBody& b,
    const glm::vec3& point,
    const glm::vec3& normal
) {
    const glm::vec3 va = a.linearVelocity + glm::cross(a.angularVelocity, point - a.position);
    const glm::vec3 vb = b.linearVelocity + glm::cross(b.angularVelocity, point - b.position);
    return glm::max(glm::dot(va - vb, normal), 0.0f);
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

    stopFastBodies();

    writeback(scene, replaying, dt);

    // After writeback, which decides what sleeps.
    if (!replaying) reportContacts(scene, ctx.events);

    // After the solve: reads this tick's contacts, so the island applies from the next tick; a contact's
    // first tick still pushes an immovable body.
    leaseContacts(scene, ctx.net);

    drawContacts(ctx);
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
    m_proxyOf.clear();
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
        if (collider) {
            syncMeshCollider(*collider, resources);
            const Terrain* terrain = scene.tryGet<Terrain>(id);
            syncTerrainCollider(*collider, terrain ? resources.tryGet(terrain->terrain) : nullptr);
        }

        // A sleeper's velocities are zeroed as it sleeps, so one set since is a launch: it wakes rather
        // than lose it.
        if (rb.sleeping && (rb.linearVelocity != glm::vec3(0.0f) || rb.angularVelocity != glm::vec3(0.0f))) {
            Rigidbody::wake(rb);
        }

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
        frame.centre = solverCentre(rb, frame.motion, collider);
        if (collider) {
            // The bound's farthest corner from the point it turns about.
            const Math::AABB bounds = colliderLocalBounds(*collider);
            const glm::vec3 low  = glm::abs(bounds.min - frame.centre);
            const glm::vec3 high = glm::abs(bounds.max - frame.centre);
            frame.reach = glm::length(glm::max(low, high));
        }

        frame.pose    = worldPoseOf(scene, id, *local);
        frame.decided = net.simulates(scene, id);
        const glm::vec3 worldPos = frame.pose.position;
        const glm::quat worldRot = frame.pose.rotation;

        const uint32_t bodyIndex = static_cast<uint32_t>(m_bodies.size());
        m_bodies.push_back(id);
        m_bodyFrames.push_back(frame);
        m_proxyOf.push_back(NO_PROXY);

        PhysicsBody pb;
        pb.position = worldPos + worldRot * frame.centre;
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
            m_proxyOf[bodyIndex] = static_cast<uint32_t>(m_proxies.size());
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

    // Neither end of a still pair can move, so nothing resolves - unless a trigger is asking. Two
    // lists keep the level from walking itself; merged, they give the pairs the order one would.
    const auto still = [](const ColliderProxy& p) { return p.immovable && !p.isTrigger; };
    m_moving.clear();
    m_still.clear();
    for (uint32_t i = 0; i < m_sorted.size(); ++i) {
        (still(m_proxies[m_sorted[i]]) ? m_still : m_moving).push_back(i);
    }

    size_t movingAfter = 0;
    size_t stillAfter  = 0;
    for (size_t a = 0; a < m_sorted.size(); ++a) {
        const ColliderProxy& pa = m_proxies[m_sorted[a]];
        const bool aStill = still(pa);
        ++(aStill ? stillAfter : movingAfter);

        size_t m = movingAfter;
        size_t s = aStill ? m_still.size() : stillAfter;
        for (;;) {
            const bool fromMoving = m < m_moving.size() && (s == m_still.size() || m_moving[m] < m_still[s]);
            if (!fromMoving && s == m_still.size()) break;
            const uint32_t b = fromMoving ? m_moving[m++] : m_still[s++];

            const ColliderProxy& pb = m_proxies[m_sorted[b]];
            if (pb.bounds.min.x > pa.bounds.max.x) break;  // sorted on X: no further overlap
            // A trigger never senses the level, or it would report the floor under it every tick.
            if ((pa.isTrigger && pb.fixed) || (pb.isTrigger && pa.fixed)) continue;

            if (!layersCollide(pa.layer, pa.collidesWith, pb.layer, pb.collidesWith)) continue;
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
                tracked.speed    = closingSpeed(
                    m_solverBodies[A.body],
                    m_solverBodies[B.body],
                    touch.point,
                    touch.normal
                );
                m_touchingThisTick.push_back(tracked);
            }

            if (A.isTrigger || B.isTrigger) continue;

            for (uint32_t m = 0; m < touch.manifoldCount; ++m, ++manifold) {
                m_manifolds.push_back(*manifold);

                // The most upward normal each body is held by: A's surface pushes along -normal, B's +normal.
                for (int k = 0; k < manifold->count; ++k) {
                    const glm::vec3& normal = manifold->contacts[k].normal;
                    if (-normal.y > m_contacts[A.body].support.y) {
                        m_contacts[A.body].support     = -normal;
                        m_contacts[A.body].supportBody = B.body;
                    }
                    if (normal.y > m_contacts[B.body].support.y) {
                        m_contacts[B.body].support     = normal;
                        m_contacts[B.body].supportBody = A.body;
                    }

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
                event.speed  = pair.speed;
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

bool PhysicsSystem::jointed(uint32_t a, uint32_t b) const {
    if (m_jointedPairs.empty()) return false;
    const uint64_t low  = glm::min(a, b);
    const uint64_t high = glm::max(a, b);
    return std::binary_search(m_jointedPairs.begin(), m_jointedPairs.end(), (low << 32) | high);
}

void PhysicsSystem::collidePair(uint32_t pair, NarrowphaseChunk& chunk) const {
    const ColliderProxy& A = m_proxies[m_pairs[pair].first];
    const ColliderProxy& B = m_proxies[m_pairs[pair].second];

    if (jointed(A.body, B.body)) return;

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
        const glm::vec3 centre = m_bodyFrames[meshProxy.body].centre;
        const glm::vec3 origin = meshBody.position + rotation * (meshPart.center - centre);
        const Math::AABB local = Math::transform(toLocal, toLocal * -origin, otherBounds);

        chunk.meshCandidates.clear();
        queryMeshBvh(meshCollider.meshNodes, local.min, local.max, chunk.meshCandidates);

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
        // From each body's centre of mass, which its solver position is; an anchor is authored from
        // the origin.
        const glm::vec3 centreB = connected != NO_BODY ? m_bodyFrames[bodyB].centre : glm::vec3(0.0f);
        constraint.anchorA = rotA * (joint.anchor - m_bodyFrames[bodyA].centre);
        constraint.anchorB = rotB * (joint.connectedAnchor - centreB);
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

    // Through no immovable body and onto no other character; see docs/reference/networking.md,
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

    net.lease(scene, m_leased);
}

void PhysicsSystem::stopFastBodies() {
    PROFILE_SCOPE("Physics/FastBodies");

    for (size_t k = 0; k < m_bodies.size(); ++k) {
        PhysicsBody& pb = m_solverBodies[k];
        if (pb.invMass == 0.0f) continue;
        const BodyFrame& frame    = m_bodyFrames[k];
        const Collider*  collider = m_proxyOf[k] != NO_PROXY ? m_proxies[m_proxyOf[k]].collider : nullptr;
        if (!collider || collider->isTrigger || collider->parts.empty()) continue;

        // Its thinnest part's half-thickness: a move past it may have crossed a surface untouched.
        float thickness = std::numeric_limits<float>::max();
        for (const ColliderPart& part : collider->parts) {
            if (part.shape == ColliderShape::Capsule) {
                thickness = std::min(thickness, part.radius);
            } else if (part.shape == ColliderShape::Box) {
                const glm::vec3& half = part.halfExtents;
                thickness = std::min(thickness, glm::min(half.x, glm::min(half.y, half.z)));
            }
        }
        if (thickness == std::numeric_limits<float>::max() || thickness <= 0.0f) continue;

        const glm::vec3 from  = frame.pose.position + frame.pose.rotation * frame.centre;
        const glm::vec3 path  = pb.position - from;
        const float     moved = glm::length(path);
        if (moved <= thickness) continue;

        // Against what it collides with and does not push: this tick's still parts, by the pairs'
        // own rules, culled by the bound the sweep runs through.
        const ColliderProxy& self  = m_proxies[m_proxyOf[k]];
        const glm::vec3      along = path / moved;
        const Math::AABB     swept{
            glm::min(from, pb.position) - glm::vec3(thickness),
            glm::max(from, pb.position) + glm::vec3(thickness)
        };
        float     nearest = moved;
        glm::vec3 normal(0.0f);
        bool      met = false;
        uint32_t  metSlot = 0;  // the entity slot of what it met
        for (const ColliderProxy& other : m_proxies) {
            if (other.body == k || other.isTrigger) continue;
            if (m_bodyFrames[other.body].motion == RigidbodyMotion::Dynamic) continue;
            if (!layersCollide(self.layer, self.collidesWith, other.layer, other.collidesWith)) continue;
            if (jointed(static_cast<uint32_t>(k), other.body) || !aabbOverlap(other.bounds, swept)) continue;

            const BodyPose& pose  = m_bodyFrames[other.body].pose;
            const glm::mat3 basis = glm::mat3_cast(pose.rotation);
            for (const ColliderPart& part : other.collider->parts) {
                float     t = 0.0f;
                glm::vec3 n(0.0f);
                const glm::vec3 centre = pose.position + basis * part.center;
                if (!sweepPart(*other.collider, part, centre, basis, from, along, thickness, nearest, t, n)) {
                    continue;
                }
                // Only a move that carries its centre past the surface crossed it: met from behind,
                // or touched by one sliding along it, the contacts have the surface in hand.
                if ((moved - t) * -glm::dot(n, along) <= thickness) continue;
                // Two met at once go to the lower slot: the proxies' order is the storage's, which
                // two ends of a session need not share.
                const uint32_t slot = m_bodies[other.body].slot();
                if (met && t == nearest && slot > metSlot) continue;
                nearest = t;
                normal  = n;
                met     = true;
                metSlot = slot;
            }
        }
        if (!met) continue;

        pb.position = from + along * nearest;
        const float into = glm::dot(pb.linearVelocity, normal);
        if (into < 0.0f) pb.linearVelocity -= into * normal;
    }
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
            const uint32_t under = m_contacts[k].supportBody;
            rb.supportBody   = rb.supported && under < m_bodies.size() ? m_bodies[under] : EntityId{};
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

        // The solve integrated the pose about the centre of mass; this only maps it home.
        Transform& t = scene.get<Transform>(id);
        const BodyFrame& frame = m_bodyFrames[k];
        const glm::vec3 origin = pb.position - pb.rotation * frame.centre;
        if (frame.pose.parented) {
            t.position = glm::vec3(glm::inverse(frame.pose.parentWorld) * glm::vec4(origin, 1.0f));
            t.rotation = glm::normalize(glm::conjugate(frame.pose.parentRot) * pb.rotation);
        } else {
            t.position = origin;
            t.rotation = pb.rotation;
        }

        // canSleep false never rests, which holds its island awake. Whether the timer may act is
        // sleepIslands' answer. A replayed tick was counted when lived.
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

namespace {

/// The longest a contact's arrow is drawn, in metres, however hard the solver pushed there.
constexpr float CONTACT_ARROW = 0.5f;

/// The push, in newtons, whose arrow is drawn half CONTACT_ARROW long: about a kilogram's weight.
constexpr float HALF_PUSH = 10.0f;

} // namespace

void PhysicsSystem::drawContacts(FrameContext& ctx) const {
    PROFILE_SCOPE("Physics/Draw");
    if (!ctx.debug || !ctx.debug->shown(DebugCategory::Contacts)) return;

    DebugDraw&       draw  = *ctx.debug;
    const DebugStyle style = DebugStyle(DebugCategory::Contacts);
    const float      dt    = ctx.clock.getFixedStep();
    for (const ContactManifold& manifold : m_manifolds) {
        // The push on B, or on A where B does not move: up out of a floor into a crate, whichever
        // was made first.
        const float into = m_bodyFrames[manifold.bodyB].invMass > 0.0f ? 1.0f : -1.0f;
        for (int c = 0; c < manifold.count; ++c) {
            const Contact& contact = manifold.contacts[c];
            draw.point(contact.point, style);
            if (contact.normalImpulse <= 0.0f) continue;  // touching, but not pushed

            // In newtons, so what rests draws alike at any tick rate.
            const float push   = contact.normalImpulse / dt;
            const float length = CONTACT_ARROW * push / (push + HALF_PUSH);
            draw.arrow(contact.point, contact.point + contact.normal * (into * length), style);
        }
    }
}

} // namespace Vkm::Engine
