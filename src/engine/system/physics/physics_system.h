#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "core/system.h"
#include "core/memory/types.h"
#include "ecs/entity.h"
#include "system/physics/physics_internal.h"
#include "system/physics/collision/contact.h"
#include "system/physics/collision/solver.h"

namespace Engine {

struct PhysicsSettings;

class EventBus;
class Scene;
struct Environment;

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
 * is not fully handled. Children parented *to* a body still follow it (markDirty
 * cascades into the subtree).
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
        void update(FrameContext& ctx) override {}
        void fixedUpdate(FrameContext& ctx) override;

        bool hasFixedUpdate() const override { return true; }

    private:
        // fixedUpdate() phases, called in order. They share the member working
        // buffers (m_bodies / m_solverBodies / m_manifolds); cross-phase plain
        // locals are threaded explicitly.

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
         * @param env   Scene-global settings (supplies the gravity vector).
         * @param dt    Fixed timestep, in seconds.
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
         * @param hasContact Per-body flags, set true for each body that gained a contact.
         * @param events     Bus the collision / trigger events are enqueued on.
         */
        void narrowphase(std::vector<bool>& hasContact, EventBus& events);

        /**
         * @brief Wake any sleeping bodies struck this step, before the solve.
         *
         * @param scene Scene whose sleeping bodies may be woken.
         */
        void wakeOnImpact(Scene& scene);

        /**
         * @brief Resolve the contact manifolds with a sequential-impulse solver.
         *
         * @param env Scene-global settings (supplies the solver iteration count).
         * @param dt  Fixed timestep, in seconds.
         */
        void solve(const PhysicsSettings& physics, float dt);

        /**
         * @brief Integrate solved velocities into poses and update sleep state.
         *
         * @param scene      Scene whose Transforms are written back.
         * @param dt         Fixed timestep, in seconds.
         * @param hasContact Per-body contact flags from narrowphase, used to decide sleeping.
         */
        void writeback(Scene& scene, float dt, const std::vector<bool>& hasContact);

    private:
        std::vector<EntityId>        m_bodies;       ///< Live body entities this tick (indexes m_solverBodies)
        std::vector<PhysicsBody>     m_solverBodies; ///< Cached dynamic state, aligned with m_bodies
        std::vector<ContactManifold> m_manifolds;    ///< Reused across ticks; clear() keeps capacity

        // Per-tick scratch, reused across ticks (clear() keeps capacity). The
        // element types live in physics_internal.h so these can be members here
        // instead of file-local statics.
        std::vector<ColliderProxy> m_proxies;     ///< Broad/narrowphase view of each collidable body (built in gather)
        std::vector<ColliderBox>   m_proxyParts;  ///< Every proxy's boxes end to end; proxies index into it
        std::vector<BodyFrame>     m_bodyFrames;  ///< World<->local frame per body, parallel to m_bodies (for writeback)

        std::vector<uint32_t>                      m_sorted;  ///< X-sorted proxy order (broadphase)
        std::vector<std::pair<uint32_t, uint32_t>> m_pairs;   ///< Candidate proxy-index pairs (broadphase)

        std::vector<SubShape> m_subA;  ///< A's child boxes expanded to world space (narrowphase)
        std::vector<SubShape> m_subB;  ///< B's child boxes expanded to world space (narrowphase)
};

} // namespace Engine
