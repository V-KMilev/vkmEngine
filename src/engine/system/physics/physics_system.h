#pragma once

#include <vector>

#include "core/system.h"
#include "core/memory/types.h"
#include "system/physics/collision/contact.h"
#include "system/physics/collision/solver.h"

namespace Engine {

class EventSystem;
class Scene;
struct PhysicsWorld;

/**
 * @brief Fixed-step rigid-body dynamics: integrates velocities, detects and
 *        resolves pairwise collisions, and writes poses back to Transform.
 *
 * Registered at SystemStage::Simulation, after AnimationSystem and before
 * HierarchySystem, so physics-updated Transforms propagate to WorldTransform the
 * same frame. All work runs in fixedUpdate() against ctx.fixedDeltaTime; update()
 * is a no-op.
 *
 * Emits CollisionEvent / TriggerEvent (enqueued, so listeners fire next flush,
 * not mid-solve) for overlapping pairs - gameplay reacts via the EventSystem or
 * the behavior onCollision/onTrigger hooks.
 *
 * A parented rigidbody simulates in world space: its WorldTransform feeds the
 * solver and the solved pose is mapped back into the local Transform relative to
 * the parent. This is correct for static / slowly-moving parents (the parent
 * pose is sampled once per tick, one frame stale); a fast-moving or scaled parent
 * is not fully handled. Children parented *to* a body still follow it (markDirty
 * cascades into the subtree).
 */
class PhysicsSystem : public System {
    public:
        explicit PhysicsSystem(EventSystem& events) : m_events(events) {}
        ~PhysicsSystem() override = default;

        PhysicsSystem(const PhysicsSystem& other) = delete;
        PhysicsSystem& operator=(const PhysicsSystem& other) = delete;

        PhysicsSystem(PhysicsSystem && other) = delete;
        PhysicsSystem& operator=(PhysicsSystem && other) = delete;

    public:
        void update(FrameContext& ctx) override {}
        void fixedUpdate(FrameContext& ctx) override;

        bool hasFixedUpdate() const override { return true; }

    private:
        // fixedUpdate() phases, called in order. They share the member working
        // buffers (m_bodies / m_solverBodies / m_manifolds) plus file-local
        // thread_local scratch; cross-phase plain locals are threaded explicitly.

        /**
         * @brief Collect the scene's simulated bodies into the working buffers.
         *
         * @param scene Scene whose rigidbody entities are gathered into m_bodies
         *              and the solver state.
         * @return False when there are no simulated bodies this step, so the
         *         remaining phases can be skipped.
         */
        bool gatherBodies(Scene& scene);

        /**
         * @brief Apply gravity and damping to body velocities.
         *
         * Frozen (kinematic / sleeping) bodies are left untouched.
         *
         * @param scene Scene providing the gathered bodies.
         * @param world Physics world parameters (gravity vector, damping).
         * @param dt    Fixed timestep, in seconds.
         */
        void integrateForces(Scene& scene, const PhysicsWorld& world, float dt);

        /**
         * @brief Sort-and-sweep the bodies into candidate collision pairs.
         */
        void broadphase();

        /**
         * @brief Run sub-shape collision tests on the candidate pairs.
         *
         * Produces the contact manifolds for the solver and fires collision events.
         *
         * @param hasContact Per-body flags, set true for each body that gained a contact.
         */
        void narrowphase(std::vector<bool>& hasContact);

        /**
         * @brief Wake any sleeping bodies struck this step, before the solve.
         *
         * @param scene Scene whose sleeping bodies may be woken.
         */
        void wakeOnImpact(Scene& scene);

        /**
         * @brief Resolve the contact manifolds with a sequential-impulse solver.
         *
         * @param world Physics world parameters (solver iteration counts, etc.).
         * @param dt    Fixed timestep, in seconds.
         */
        void solve(const PhysicsWorld& world, float dt);

        /**
         * @brief Integrate solved velocities into poses and update sleep state.
         *
         * @param scene      Scene whose Transforms are written back.
         * @param dt         Fixed timestep, in seconds.
         * @param hasContact Per-body contact flags from narrowphase, used to decide sleeping.
         */
        void writeback(Scene& scene, float dt, const std::vector<bool>& hasContact);

    private:
        EventSystem& m_events;  ///< Collision/trigger events are enqueued here.

        std::vector<EntityId>        m_bodies;       ///< Live body entities this tick (indexes m_solverBodies)
        std::vector<PhysicsBody>     m_solverBodies; ///< Cached dynamic state, aligned with m_bodies
        std::vector<ContactManifold> m_manifolds;    ///< Reused across ticks; clear() keeps capacity
};

} // namespace Engine
