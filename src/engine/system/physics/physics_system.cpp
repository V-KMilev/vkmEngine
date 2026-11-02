#include "system/physics/physics_system.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vkm_log/assert.h"

#include "vkm_physics/mass_model.h"
#include "vkm_physics/workers.h"

#include "core/clock.h"
#include "debug/draw/debug_draw.h"
#include "debug/error/engine_error_log.h"
#include "base/profile/profiler.h"
#include "ecs/scene.h"
#include "ecs/settings/environment.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/hierarchy_operations.h"
#include "base/event/event_bus.h"
#include "net/net_session.h"
#include "base/math/axes.h"
#include "base/math/rotation.h"
#include "vkm_platform/threading/thread_pool.h"
#include "resource/resource_manager.h"
#include "ecs/component/physics/mesh_collider.h"
#include "core/frame/body_pose.h"
#include "core/frame/physics_events.h"
#include "ecs/component/physics/ragdoll.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief The step's threads: the engine's pool, a chunk to a claim.
 */
class PoolWorkers final : public Physics::Workers {
    public:
        void forEach(size_t count, const Physics::Job& job) override {
            Platform::parallelFor(count, 1, [&](size_t index) { job.run(job.state, index); });
        }
};

float horizontalLengthSq(const glm::vec3& normal) {
    return normal.x * normal.x + normal.z * normal.z;
}

} // namespace

void PhysicsSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("PhysicsSystem");

    Scene& scene = ctx.scene;

    // A replacement world reuses the slots the step's ids are, so it would seed contacts never made.
    // Its touching pairs go unreported: their ids now name the new world's entities.
    if (scene.epoch() != m_worldEpoch) {
        m_stepper.forget();
        m_touchingLastTick.clear();
        m_worldEpoch = scene.epoch();
    }

    const bool replaying = ctx.net.replaying();

    if (!gatherBodies(scene, ctx.resources, ctx.net)) {
        // Nothing touches, so whatever touched on the last live tick has ended.
        if (!replaying) reportContacts(scene, ctx.events, {});
        return;
    }

    gatherJoints(scene);

    const PhysicsSettings& physics = scene.physics();
    Physics::StepParams params;
    params.gravity    = physics.gravity;
    params.iterations = solverPasses(physics);
    params.dt         = ctx.clock.getFixedStep();
    params.replaying  = replaying;
    PoolWorkers workers;
    m_stepper.step(m_bodies, m_parts, m_joints, params, workers);

    writeback(scene, replaying);

    // After writeback, which decides what sleeps.
    if (!replaying) reportContacts(scene, ctx.events, m_stepper.touching());

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
    m_parts.clear();
    m_entities.clear();
    m_rigidbodies.clear();
    m_parents.clear();

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
            // TerrainSystem syncs it too, but later in the frame: a terrain loaded or stroked since would
            // be fallen through for a tick.
            const Terrain* terrain = scene.tryGet<Terrain>(id);
            syncTerrainCollider(*collider, terrain ? resources.tryGet(terrain->terrain) : nullptr);
        }

        if (rb.motion == RigidbodyMotion::Dynamic && rb.mass <= 0.0f && m_massless.report()) {
            const Name* name = scene.tryGet<Name>(id);
            reportError("Physics", name ? name->value : "a body", "a Dynamic body needs a positive mass");
        }

        // A bone an inactive ragdoll poses is a hitbox, placed by the animation: kinematic, with no contacts.
        const bool posed = idx < m_posedBone.size() && m_posedBone[idx];

        Physics::Body body;
        body.id             = id.slot();
        body.motion         = posed ? Physics::Motion::Kinematic : physicsMotionOf(rb.motion);
        body.simulated      = net.simulates(scene, id);
        body.mass           = rb.mass;
        body.linearDamping  = rb.linearDamping;
        body.angularDamping = rb.angularDamping;
        body.restitution    = rb.restitution;
        body.friction       = rb.friction;
        body.gravityScale   = rb.gravityScale;
        body.layer          = rb.layer;
        body.collidesWith   = rb.collidesWith;
        body.freezeRotation = rb.freezeRotation;
        body.canSleep       = rb.canSleep;

        // Its parts, which a disabled collider still has for its mass.
        body.firstPart = static_cast<uint32_t>(m_parts.size());
        if (collider) {
            for (const ColliderPart& part : collider->parts) m_parts.push_back(physicsPartOf(part));
        }
        body.partCount = static_cast<uint32_t>(m_parts.size()) - body.firstPart;
        body.triangles = collider ? &collider->triangles : nullptr;
        body.trigger   = collider && collider->isTrigger;
        body.collides  = collider && collider->enabled && !posed;

        const BodyPose pose = worldPoseOf(scene, id, *local);
        body.position        = pose.position;
        body.rotation        = pose.rotation;
        body.linearVelocity  = rb.linearVelocity;
        body.angularVelocity = rb.angularVelocity;
        body.sleeping        = rb.sleeping;
        body.sleepTimer      = rb.sleepTimer;

        m_bodies.push_back(body);
        m_entities.push_back(id);
        m_rigidbodies.push_back(&rb);
        m_parents.push_back(pose.parentWorld);
    }

    m_massless.endPass();

    return !m_bodies.empty();
}

void PhysicsSystem::gatherJoints(Scene& scene) {
    PROFILE_SCOPE("Physics/GatherJoints");

    m_joints.clear();
    m_jointOf.clear();

    auto* storage = scene.storage<Joint>();
    if (!storage) return;

    // From the bodies gathered, so an entity not simulated this tick resolves to NO_BODY.
    uint32_t maxSlot = 0;
    for (const EntityId body : m_entities) maxSlot = std::max(maxSlot, body.slot());
    m_bodyIndexBySlot.assign(static_cast<size_t>(maxSlot) + 1, Physics::NO_BODY);
    for (uint32_t i = 0; i < m_entities.size(); ++i) m_bodyIndexBySlot[m_entities[i].slot()] = i;

    const auto bodyIndexOf = [&](uint32_t slot) {
        return slot < m_bodyIndexBySlot.size() ? m_bodyIndexBySlot[slot] : Physics::NO_BODY;
    };

    const uint32_t count = static_cast<uint32_t>(storage->size());
    for (uint32_t i = 0; i < count; ++i) {
        const EntityId self = scene.entityAt(storage->keyAt(i));
        Joint& joint = storage->dataAt(i);
        // Kept only by a joint solved this tick: one skipped starts again from nothing.
        const glm::vec3 impulse     = joint.impulse;
        const glm::vec3 holdImpulse = joint.holdImpulse;

        joint.impulse     = glm::vec3(0.0f);
        joint.holdImpulse = glm::vec3(0.0f);
        // Checked as a handle, not a slot: a dead entity's slot is reused.
        if (!joint.connected || !scene.isAlive(joint.connected)) continue;
        // A body held to itself is held to nothing.
        if (joint.connected == self) continue;

        Physics::Joint handed;
        handed.bodyA = bodyIndexOf(self.slot());
        if (handed.bodyA == Physics::NO_BODY) continue;

        handed.bodyB = bodyIndexOf(joint.connected.slot());
        if (handed.bodyB == Physics::NO_BODY) {
            // A pose but no body: the joint holds to that world point.
            if (!scene.has<Transform>(joint.connected)) continue;
            const glm::mat4 world = HierarchyOperations::computeWorldMatrix(scene, joint.connected);
            handed.fixedPosition = glm::vec3(world[3]);
            handed.fixedRotation = Math::worldRotationOf(world);
        }

        handed.type             = physicsJointTypeOf(joint.type);
        handed.anchor           = joint.anchor;
        handed.connectedAnchor  = joint.connectedAnchor;
        handed.distance         = joint.distance;
        handed.stiffness        = joint.stiffness;
        handed.holdTorque       = joint.holdTorque;
        handed.collideConnected = joint.collideConnected;
        handed.resolvedDistance = joint.resolvedDistance;
        handed.heldRotation     = joint.heldRotation;
        handed.heldResolved     = joint.heldResolved;
        handed.impulse          = impulse;
        handed.holdImpulse      = holdImpulse;

        m_joints.push_back(handed);
        m_jointOf.push_back(&joint);
    }
}

int PhysicsSystem::solverPasses(const PhysicsSettings& physics) {
    const int asked  = physics.solverIterations;
    const int passes = std::clamp(
        asked,
        PhysicsSettings::MIN_SOLVER_ITERATIONS,
        PhysicsSettings::MAX_SOLVER_ITERATIONS
    );
    if (passes != asked && m_passesOutside.report()) {
        std::string said = std::to_string(asked) + " solver passes a step is outside ";
        said += std::to_string(PhysicsSettings::MIN_SOLVER_ITERATIONS) + " to ";
        said += std::to_string(PhysicsSettings::MAX_SOLVER_ITERATIONS) + "; the step runs ";
        said += std::to_string(passes);
        reportError("Physics", "the scene's solverIterations", said);
    }
    m_passesOutside.endPass();
    return passes;
}

void PhysicsSystem::writeback(Scene& scene, bool replaying) {
    PROFILE_SCOPE("Physics/Writeback");

    // Each body's most upward and most horizontal contact normal, as the surface acts on it: A's pushes
    // along -normal, B's along +normal. The step keeps only resolved manifolds, so a trigger holds nothing up.
    m_support.assign(m_bodies.size(), Support{});
    for (const Physics::ContactManifold& manifold : m_stepper.manifolds()) {
        Support& a = m_support[manifold.bodyA];
        Support& b = m_support[manifold.bodyB];
        a.touched = true;
        b.touched = true;
        for (int c = 0; c < manifold.count; ++c) {
            const glm::vec3& normal = manifold.contacts[c].normal;
            if (-normal.y > a.normal.y) {
                a.normal = -normal;
                a.body   = manifold.bodyB;
            }
            if (normal.y > b.normal.y) {
                b.normal = normal;
                b.body   = manifold.bodyA;
            }

            // The normals differ only in sign, so one measure serves both.
            const float horizontal = horizontalLengthSq(normal);
            if (horizontal > horizontalLengthSq(a.block)) a.block = -normal;
            if (horizontal > horizontalLengthSq(b.block)) b.block = normal;
        }
    }

    for (size_t k = 0; k < m_bodies.size(); ++k) {
        const Physics::Body& body = m_bodies[k];
        Rigidbody& rb = *m_rigidbodies[k];

        // A replay publishes no support for a body this end does not decide.
        if (body.simulated || !replaying) {
            const Support& support = m_support[k];
            VKM_ASSERT(!support.touched || support.body != Physics::NO_BODY, "a touch whose contacts name no body");
            // Named with the normal it gave, so a touch that named none supports nothing.
            const bool supported = support.body != Physics::NO_BODY;
            rb.supported     = supported;
            rb.supportNormal = supported ? support.normal : Math::WORLD_UP;
            rb.blockNormal   = supported ? support.block : Math::WORLD_UP;
            rb.supportBody   = supported ? m_entities[support.body] : EntityId{};
        }

        rb.linearVelocity  = body.linearVelocity;
        rb.angularVelocity = body.angularVelocity;
        rb.sleeping        = body.sleeping;
        rb.sleepTimer      = body.sleepTimer;
        if (!body.moved) continue;

        Transform& t = scene.get<Transform>(m_entities[k]);
        const glm::mat4& parent = m_parents[k];
        if (parent == glm::mat4(1.0f)) {
            t.position = body.position;
            t.rotation = body.rotation;
        } else {
            t.position = glm::vec3(glm::inverse(parent) * glm::vec4(body.position, 1.0f));
            t.rotation = glm::normalize(glm::conjugate(Math::worldRotationOf(parent)) * body.rotation);
        }
    }

    for (size_t j = 0; j < m_joints.size(); ++j) {
        const Physics::Joint& handed = m_joints[j];
        Joint& joint = *m_jointOf[j];

        joint.resolvedDistance = handed.resolvedDistance;
        joint.heldRotation     = handed.heldRotation;
        joint.heldResolved     = handed.heldResolved;
        joint.impulse          = handed.impulse;
        joint.holdImpulse      = handed.holdImpulse;
    }
}

void PhysicsSystem::reportContacts(
    const Scene& scene,
    EventBus& events,
    const std::vector<Physics::Touch>& touches
) {
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

    m_touching.clear();
    for (const Physics::Touch& touch : touches) {
        TrackedPair pair;
        pair.a        = m_entities[touch.a];
        pair.b        = m_entities[touch.b];
        pair.aTrigger = m_bodies[touch.a].trigger;
        pair.bTrigger = m_bodies[touch.b].trigger;
        pair.point    = touch.point;
        pair.normal   = touch.normal;
        pair.speed    = touch.speed;
        m_touching.push_back(pair);
    }

    std::vector<TrackedPair>& now  = m_touching;
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

void PhysicsSystem::leaseContacts(Scene& scene, NetSession& net) {
    PROFILE_SCOPE("Physics/Lease");

    if (net.role() != NetRole::Client) return;

    const EntityId owner = net.localEntity();
    if (!owner || !scene.isAlive(owner)) return;

    const std::vector<EntityId>& bodies = m_entities;
    uint32_t ownerBody = UINT32_MAX;
    for (uint32_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i] == owner) {
            ownerBody = i;
            break;
        }
    }
    if (ownerBody == UINT32_MAX) return;

    // Through no immovable body and onto no other character; see docs/reference/networking.md,
    // "Leasing".
    const uint32_t count = static_cast<uint32_t>(bodies.size());
    std::vector<uint8_t>& carries = m_leaseCarries;
    carries.assign(count, 0);
    for (uint32_t k = 0; k < count; ++k) {
        const float invMass = Physics::inverseMassOf(m_bodies[k].motion, m_bodies[k].mass);
        carries[k] = k == ownerBody || (invMass > 0.0f && !scene.has<CharacterController>(bodies[k]));
    }

    m_leaseIslands.reset(count);
    const auto join = [&](uint32_t a, uint32_t b) {
        if (a < count && b < count && carries[a] && carries[b]) m_leaseIslands.join(a, b);
    };
    for (const Physics::ContactManifold& manifold : m_stepper.manifolds()) {
        join(manifold.bodyA, manifold.bodyB);
    }
    for (const Physics::Joint& joint : m_joints) join(joint.bodyA, joint.bodyB);

    m_leased.clear();
    const uint32_t island = m_leaseIslands.find(ownerBody);
    for (uint32_t k = 0; k < count; ++k) {
        if (k != ownerBody && m_leaseIslands.find(k) == island) m_leased.push_back(bodies[k]);
    }

    net.lease(scene, m_leased);
}

namespace {

constexpr float CONTACT_ARROW = 0.5f;  ///< The longest a contact's arrow is drawn, in meters, however hard
                                       ///< the solver pushed there.

constexpr float HALF_PUSH = 10.0f;  ///< The push, in newtons, whose arrow is drawn half CONTACT_ARROW long:
                                    ///< about a kilogram's weight.

} // namespace

void PhysicsSystem::drawContacts(FrameContext& ctx) const {
    PROFILE_SCOPE("Physics/Draw");
    if (!ctx.debug || !ctx.debug->shown(DebugCategory::Contacts)) return;

    DebugDraw& draw = *ctx.debug;
    const DebugStyle style = DebugStyle(DebugCategory::Contacts);
    const float dt = ctx.clock.getFixedStep();
    for (const Physics::ContactManifold& manifold : m_stepper.manifolds()) {
        // The push on B, or on A where B does not move: up out of a floor into a crate, whichever
        // was made first.
        const Physics::Body& b = m_bodies[manifold.bodyB];
        const float into = Physics::inverseMassOf(b.motion, b.mass) > 0.0f ? 1.0f : -1.0f;
        for (int c = 0; c < manifold.count; ++c) {
            const Physics::Contact& contact = manifold.contacts[c];
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
