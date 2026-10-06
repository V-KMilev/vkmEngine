#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/system.h"
#include "debug/fault_latch.h"
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
class ResourceManager;
class Scene;

/**
 * @brief Fixed-step rigid-body dynamics: integrates, resolves pairwise collisions, writes poses back.
 *
 * Runs on the tick in SystemStage::Simulation, ahead of HierarchySystem, so the Transforms it writes
 * reach WorldTransform the same frame. Enqueues CollisionEvent / TriggerEvent as contacts begin, last
 * and end, on live ticks only; a replayed tick already reported.
 *
 * A parented body simulates in world space (worldPoseOf) and is mapped back relative to its parent. The
 * parent's pose is taken once at gather, so a parent moving mid-tick is not followed, and a scaled
 * parent is not handled (a ColliderPart is unscaled).
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

        /**
         * @brief Re-run during a replay: it moves the world from state and command, deterministically.
         *
         * @return Always true.
         */
        bool isReplayed() const override { return true; }

    private:
        /// What a joint held when a tick ended, to start the next from.
        struct HeldJoint {
            EntityId  owner;        ///< The entity carrying the Joint
            glm::vec3 impulse;      ///< Its anchors', world space
            glm::vec3 holdImpulse;  ///< Its hold's, world space
        };

    private:
        /**
         * @brief Collect the scene's simulated bodies into the working buffers.
         *
         * A ragdoll bone the animation still drives is gathered without a collider: it is kinematic, so a
         * character's legs would shove any crate with infinite mass. A query still finds it, as a hitbox.
         *
         * @param scene     Scene whose rigidbody entities are gathered.
         * @param resources Where a mesh collider's mesh resolves; its triangles are synced here.
         * @param net       Who decides each body; one this end does not decide is gathered immovable.
         * @return False when there are no simulated bodies, so the remaining phases can be skipped.
         */
        bool gatherBodies(Scene& scene, const ResourceManager& resources, const NetSession& net);

        /**
         * @brief Apply gravity and damping to body velocities.
         *
         * Static, kinematic, sleeping and undecided bodies are left untouched.
         *
         * @param physics Supplies the gravity vector.
         * @param dt      Fixed timestep, seconds.
         */
        void integrateForces(const PhysicsSettings& physics, float dt);

        /**
         * @brief Sort-and-sweep the bodies into candidate collision pairs.
         */
        void broadphase();

        /**
         * @brief Run sub-shape collision tests on the candidate pairs.
         *
         * Chunks run across the pool (collidePair), then merge in pair order into m_manifolds,
         * m_contacts and m_touchingThisTick, bit-identical to one thread. A body with no contact keeps
         * the seeded sentinels, which writeback resolves.
         *
         * @param replaying True while re-running ticks the server disagreed with; touching pairs are
         *        then not tracked.
         */
        void narrowphase(bool replaying);

        /**
         * @brief Collide the parts of one broadphase pair, into @p chunk.
         *
         * Writes only @p chunk, so any thread may run it.
         *
         * @param pair  Index into m_pairs.
         * @param chunk Gets the pair's manifolds, and a TouchingPair if it touched.
         */
        void collidePair(uint32_t pair, NarrowphaseChunk& chunk) const;

        /**
         * @brief Group the tick's dynamic bodies into islands, and wake every disturbed sleeping island.
         *
         * Members are what isIslandMember admits, joined by contacts and joints. A sleeping island wakes
         * whole - a sleeper is a wall to the solver - when a member is awake, when an outside body that
         * can push it touches it (awake and undecided, or driven kinematic), or when nothing outside holds
         * it. sleepIslands reuses these islands, as the solve changes no contact or joint.
         */
        void buildIslands();

        /**
         * @brief Whether @p body takes part in the islands: a dynamic body this end decides, asleep or not.
         *
         * @param body Index into this tick's bodies; a joint's world anchor past their end is not one.
         * @return True for a member.
         */
        bool isIslandMember(uint32_t body) const;

        /**
         * @brief Resolve each Joint component to the two bodies it constrains.
         *
         * Anchors become world-space lever arms. Dropped: a joint whose entity is not simulated, that
         * names itself, or names an entity with neither body nor pose. A connected entity with a pose but
         * no body is a fixed world point, appended past the real bodies.
         *
         * @param scene Scene whose Joint components are gathered.
         */
        void gatherJoints(Scene& scene);

        /**
         * @brief Solve the tick's contacts and joints, warm-started across ticks, and integrate the poses.
         *
         * @param physics Supplies the solver iteration count.
         * @param dt      Fixed timestep, seconds.
         */
        void solve(const PhysicsSettings& physics, float dt);

        /**
         * @brief Put whole islands to sleep, or none of their members.
         *
         * A still body on a settling stack may not sleep. An island sleeps only while something outside
         * holds it (a contact or joint). Uses the islands buildIslands made.
         *
         * @param replaying True during a replay, where nothing sleeps: that is a live-timeline decision.
         */
        void sleepIslands(bool replaying);

        /**
         * @brief Write the solved poses back to Transform and update sleep state.
         *
         * Publishes m_contacts onto each Rigidbody and advances each awake body's rest timer.
         *
         * @param scene     Scene whose Transforms are written back.
         * @param replaying True during a replay, where undecided bodies get no support published.
         * @param dt        Fixed timestep, seconds.
         */
        void writeback(Scene& scene, bool replaying, float dt);

        /**
         * @brief Report how this live tick's touching pairs differ from the last live tick's, and keep them.
         *
         * In both: stayed; only now: began; only before: ended, whatever the cause. A pair whose bodies
         * both rest sends no Stayed, and wakes without beginning again. In TrackedPair::before order.
         * Never called in a replay.
         *
         * @param scene  The world, for whether a pair's bodies rest.
         * @param events Bus the collision / trigger events are enqueued on.
         */
        void reportContacts(const Scene& scene, EventBus& events);

        /**
         * @brief Tell the session which bodies this end's own character is on.
         *
         * A client predicts only what it owns, so a push would predict a wall. This hands the session the
         * island of pushable bodies, through this tick's contacts and joints, the owned body is in. Only on
         * a client with its own character.
         *
         * @param scene The world, for turning body indices back into entities.
         * @param net   The session to report the island to.
         */
        void leaseContacts(Scene& scene, NetSession& net);

    private:
        std::vector<EntityId>        m_bodies;  ///< Live body entities this tick (indexes m_solverBodies)

        /**
         * @brief Cached dynamic state, aligned with m_bodies, then longer.
         *
         * Joint world anchors are appended past the real bodies, so a walk mapping results back to
         * entities walks m_bodies.
         */
        std::vector<PhysicsBody>     m_solverBodies;
        std::vector<EntityId>        m_leased;          ///< Scratch for leaseContacts
        std::vector<uint8_t>         m_leaseCarries;    ///< Per body: a push passes through it
        BodyIslands                  m_leaseIslands;    ///< What those join into; storage kept
        std::vector<ContactManifold> m_manifolds;       ///< Reused across ticks; clear() keeps capacity
        /// What held each pair last tick, to start this one from
        ContactCache                 m_contactCache;
        uint64_t                     m_worldEpoch = 0;  ///< Scene::epoch() the cache was filled in
        std::vector<JointConstraint> m_joints;          ///< This tick's joints, as body indices

        /**
         * @brief Each joint's impulses at the end of last tick, world space, by entity in slot order.
         *
         * The joints' m_contactCache: a chain solved from zero every tick hangs stretched. Not rolled back.
         */
        std::vector<HeldJoint> m_jointImpulses;

        /**
         * @brief Entity slot -> this tick's body index, or NO_BODY.
         *
         * A vector: the key is already a dense small integer (see SlotAllocator).
         */
        std::vector<uint32_t> m_bodyIndexBySlot;

        /**
         * @brief Body pairs a joint holds together, packed as (low << 32) | high and sorted.
         *
         * Jointed bodies overlap by construction (thigh and shin share the knee), so the narrowphase
         * skips their contact unless the joint says otherwise.
         */
        std::vector<uint64_t> m_jointedPairs;

        /// Broad/narrowphase view of each collidable body
        std::vector<ColliderProxy> m_proxies;
        /// World<->local frame per body, parallel to m_bodies (for writeback)
        std::vector<BodyFrame>     m_bodyFrames;

        /// This tick's sleep islands; storage kept
        BodyIslands                m_islands;
        /// Per-island "something outside it holds it", by root
        std::vector<uint8_t>       m_islandHeld;
        std::vector<uint8_t>       m_islandDisturbed;  ///< Per-island "something moves it this tick", by root
        /// Per-island "every member has held still long enough", by root
        std::vector<uint8_t>       m_islandReady;
        /// Per-body contact summary, spanning narrowphase -> writeback
        std::vector<BodyContacts>  m_contacts;

        /**
         * @brief The pairs touching on this live tick, from the narrowphase to reportContacts.
         */
        std::vector<TrackedPair> m_touchingThisTick;

        /**
         * @brief The pairs touching when the last live tick ended, in TrackedPair::before order.
         *
         * Event state, not simulation state: a replay leaves it alone, a replaced world drops it unreported.
         */
        std::vector<TrackedPair> m_touchingLastTick;

        FaultLatch m_massless;  ///< A Dynamic body with no positive mass

        std::vector<uint32_t>                      m_sorted;  ///< X-sorted proxy order (broadphase)
        std::vector<std::pair<uint32_t, uint32_t>> m_pairs;   ///< Candidate proxy-index pairs (broadphase)

        std::vector<BoxShape>     m_shapeBoxes;         ///< Every proxy's boxes in world space, end to end
        std::vector<Math::AABB>   m_shapeBoxBounds;     ///< World bound of each box, parallel to m_shapeBoxes
        std::vector<CapsuleShape> m_shapeCapsules;      ///< Every proxy's capsules in world space, end to end
        /// World bound of each capsule, parallel to m_shapeCapsules
        std::vector<Math::AABB>   m_shapeCapsuleBounds;

        /**
         * @brief One per chunk of the narrowphase's pairs, in pair order.
         *
         * Never shrunk, so the buffers inside are not reallocated on the next busy tick.
         */
        std::vector<NarrowphaseChunk> m_narrowphaseChunks;

        std::vector<bool> m_posedBone;  ///< By slot: markPosedByAnimation's answer this tick
};

} // namespace Vkm::Engine
