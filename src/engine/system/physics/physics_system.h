#pragma once

#include <vector>

#include "core/system.h"
#include "core/memory/types.h"
#include "system/physics/collision/contact.h"
#include "system/physics/collision/solver.h"

namespace Engine {

class EventSystem;

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
        EventSystem& m_events;  ///< Collision/trigger events are enqueued here.

        std::vector<EntityId>        m_bodies;       ///< Live body entities this tick (indexes m_solverBodies)
        std::vector<PhysicsBody>     m_solverBodies; ///< Cached dynamic state, aligned with m_bodies
        std::vector<ContactManifold> m_manifolds;    ///< Reused across ticks; clear() keeps capacity
};

} // namespace Engine
