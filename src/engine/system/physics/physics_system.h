#pragma once

#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/system.h"
#include "ecs/entity.h"
#include "system/physics/physics_internal.h"
#include "system/physics/collision/contact.h"
#include "system/physics/collision/narrowphase.h"
#include "system/physics/collision/support.h"
#include "system/physics/solver/joint_solver.h"
#include "system/physics/solver/solver.h"
#include "system/physics/solver/contact_cache.h"

namespace Vkm::Engine {

class NetSession;

struct PhysicsSettings;

class EventBus;
class Scene;

/**
 * @brief Fixed-step rigid-body dynamics: integrates velocities, detects and
 *        resolves pairwise collisions, and writes poses back to Transform.
 *
 * Registered at SystemStage::Simulation, after AnimationSystem and before
 * HierarchySystem, so physics-updated Transforms propagate to WorldTransform the
 * same frame. All work runs in fixedUpdate() against ctx.clock.getFixedStep(); update()
 * is a no-op.
 *
 * Emits CollisionEvent / TriggerEvent (enqueued, so listeners fire next flush,
 * not mid-solve) for overlapping pairs - gameplay reacts via the EventBus or
 * the behavior onCollision/onTrigger hooks.
 *
 * A parented rigidbody simulates in world space: its WorldTransform feeds the
 * solver and the solved pose is mapped back into the local Transform relative to
 * the parent. This is correct for static / slowly-moving parents (the parent
 * pose is sampled once per tick, one frame stale); a fast-moving or scaled parent
 * is not fully handled. Children parented *to* a body still follow it, because
 * HierarchySystem rebuilds the whole subtree later in the same frame.
 */
class PhysicsSystem : public System {
    public:
        PhysicsSystem() = default;
        ~PhysicsSystem() override = default;

        PhysicsSystem(const PhysicsSystem& other) = delete;
        PhysicsSystem& operator=(const PhysicsSystem& other) = delete;

        PhysicsSystem(PhysicsSystem && other) = delete;
        PhysicsSystem& operator=(PhysicsSystem && other) = delete;

    public:
        void fixedUpdate(FrameContext& ctx) override;

        bool hasFixedUpdate() const override { return true; }

        /**
         * @brief Re-run during a replay: it moves the world from state and command, and
         * running it twice over the same tick lands in the same place.
         */
        bool isReplayed() const override { return true; }

    private:
        // fixedUpdate() phases, called in order. They share the member working
        // buffers (m_bodies / m_solverBodies / m_manifolds); cross-phase plain
        // locals are threaded explicitly.

        /**
         * @brief Collect the scene's simulated bodies into the working buffers.
         *
         * @param scene Scene whose rigidbody entities are gathered into m_bodies
         *              and the solver state.
         * @param net   Who decides each body. Asked once here and stored on the
         *              BodyFrame, because every phase after this one needs it:
         *              a body this end does not decide is gathered immovable,
         *              so it still collides and is never moved.
         * @return False when there are no simulated bodies this step, so the
         *         remaining phases can be skipped.
         */
        bool gatherBodies(Scene& scene, const NetSession& net);

        /**
         * @brief Apply gravity and damping to body velocities.
         *
         * Frozen (kinematic / sleeping) bodies are left untouched.
         *
         * @param scene   Scene providing the gathered bodies.
         * @param physics Scene's physics settings (supplies the gravity vector).
         * @param dt      Fixed timestep, in seconds.
         */
        void integrateForces(Scene& scene, const PhysicsSettings& physics, float dt);

        /**
         * @brief Sort-and-sweep the bodies into candidate collision pairs.
         */
        void broadphase();

        /**
         * @brief Run sub-shape collision tests on the candidate pairs.
         *
         * Produces the contact manifolds for the solver and fires collision events.
         *
         * Fills m_contacts as it goes: a body that gains none is left holding
         * the seeded sentinels rather than directions, and writeback is what
         * turns them into one.
         *
         * @param events Bus the collision / trigger events are enqueued on.
         * @param replaying True while re-running ticks the server disagreed
         *        with. Contacts are still produced; the events are not, because
         *        the tick they belong to already reported them and a behavior
         *        acting on the second report acts on something that did not
         *        happen twice.
         */
        void narrowphase(EventBus& events, bool replaying);

        /**
         * @brief Wake any sleeping bodies struck this step, before the solve.
         *
         * @param scene Scene whose sleeping bodies may be woken.
         */
        void wakeConnected(Scene& scene);

        /**
         * @brief Resolve each Joint component to the two bodies it constrains.
         *
         * Anchors become world-space lever arms here, so the solver never looks
         * at the scene. A joint naming a body that is not simulated this tick -
         * destroyed, or without a Rigidbody - is dropped rather than held
         * against a body index that means something else.
         *
         * @param scene Scene whose Joint components are gathered.
         */
        void gatherJoints(Scene& scene);

        /**
         * @brief Resolve the contact manifolds with a sequential-impulse solver.
         *
         * @param physics Scene's physics settings (supplies the solver iteration count).
         * @param dt      Fixed timestep, in seconds.
         */
        void solve(const PhysicsSettings& physics, float dt);

        /**
         * @brief Integrate solved velocities into poses and update sleep state.
         *
         * Reads m_contacts: the touched flag decides sleeping, and both normals
         * are published onto the Rigidbody for whatever reads them.
         *
         * @param scene     Scene whose Transforms are written back.
         * @param replaying  True while re-running ticks the server disagreed
         *                   with. Support is published for every body except,
         *                   in a replay, one this end does not decide.
         * @param dt        Fixed timestep, in seconds.
         */
        /**
         * @brief Put whole contact islands to sleep, or none of their members.
         *
         * A body resting alone may sleep; a body resting on a stack that is
         * still settling may not, however still it happens to be this tick.
         * Everything that can push everything else sleeps together.
         */
        void sleepIslands(Scene& scene, bool replaying);

        void writeback(Scene& scene, bool replaying, float dt);

        /**
         * @brief Tell the session which bodies this end's own character is on.
         *
         * A client predicts only what it owns, so a crate is immovable to it
         * and a push is predicted as walking into a wall - then corrected when
         * the server says the crate moved. This walks outward from the owned
         * body across the contacts and joints of the tick that just ran and
         * hands the session the island it found, which is what makes the push
         * predictable rather than merely correctable.
         *
         * Only on a client with a character of its own. A server and an offline
         * session already decide everything, so they pay one comparison.
         *
         * @param scene The world, for turning body indices back into entities.
         * @param net   The session to report the island to.
         */
        void leaseContacts(Scene& scene, NetSession& net);

    private:
        std::vector<EntityId>        m_bodies;       ///< Live body entities this tick (indexes m_solverBodies)

        /**
         * @brief Cached dynamic state, aligned with m_bodies - and then longer.
         *
         * A joint whose other end has a pose but no rigidbody pins to that point
         * in the world, and the solver reaches it the only way it reaches
         * anything: as a body index. Those anchors are appended past the real
         * bodies, so `m_solverBodies.size() >= m_bodies.size()` and everything
         * that walks bodies to write results back - writeback, the island pass,
         * the joint sort's slot lookup - walks m_bodies and therefore stops
         * before them. That is the whole mechanism: an anchor is a body nothing
         * owns, so nothing writes it anywhere.
         */
        std::vector<PhysicsBody>     m_solverBodies;
        std::vector<EntityId>        m_leased;       ///< Scratch for leaseContacts
        std::vector<uint32_t>        m_leaseFrontier;  ///< Its breadth-first queue
        std::vector<bool>            m_leaseReached; ///< Which bodies it has already taken
        std::vector<ContactManifold> m_manifolds;    ///< Reused across ticks; clear() keeps capacity
        ContactCache                 m_contactCache; ///< What held each pair last tick, to start this one from
        std::vector<JointConstraint> m_joints;       ///< This tick's joints, as body indices

        /**
         * @brief Entity slot -> this tick's body index, or NO_BODY.
         *
         * A vector rather than a hash map because the key is already a dense
         * small integer: the slot allocator hands them out from zero and
         * recycles them. Kept across ticks for its capacity - it is refilled
         * every tick, never grown from nothing.
         */
        std::vector<uint32_t> m_bodyIndexBySlot;

        /// No body this tick holds that slot.
        static constexpr uint32_t NO_BODY = ~0u;

        /**
         * @brief Body pairs a joint holds together, packed as (low << 32) | high.
         *
         * Jointed bodies overlap by construction - a thigh and a shin share the
         * knee - so resolving both the contact and the joint means the solver
         * pushing them apart while the joint pulls them together. The two pump
         * energy in until the ragdoll leaves the level. Skipped here unless the
         * joint says otherwise.
         */
        std::unordered_set<uint64_t> m_jointedPairs;

        // Per-tick scratch, reused across ticks (clear() keeps capacity). The
        // element types live in physics_internal.h so these can be members here
        // instead of file-local statics.
        std::vector<ColliderProxy> m_proxies;     ///< Broad/narrowphase view of each collidable body (built in gather)
        std::vector<ColliderPart>  m_proxyParts;  ///< Every proxy's parts, end to end
        std::vector<BodyFrame>     m_bodyFrames;  ///< World<->local frame per body, parallel to m_bodies (for writeback)
        std::vector<BodyContacts>  m_contacts;    ///< Per-body contact summary, spanning narrowphase -> writeback

        std::vector<uint32_t>                      m_sorted;  ///< X-sorted proxy order (broadphase)
        std::vector<std::pair<uint32_t, uint32_t>> m_pairs;   ///< Candidate proxy-index pairs (broadphase)

        std::vector<BoxShape>     m_shapeBoxes;    ///< Every proxy's boxes in world space, end to end
        std::vector<CapsuleShape> m_shapeCapsules; ///< Every proxy's capsules in world space, end to end

        std::vector<uint32_t> m_meshCandidates;  ///< Triangles a tree walk found
};

} // namespace Vkm::Engine
