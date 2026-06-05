#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/system.h"
#include "core/memory/types.h"
#include "system/physics/collision/contact.h"
#include "system/physics/collision/solver.h"

namespace Engine {

/**
 * @brief Fixed-step rigid-body dynamics: integrates velocities, detects and
 *        resolves pairwise collisions, and writes poses back to Transform.
 *
 * Registered at SystemStage::Simulation, after AnimationSystem and before
 * HierarchySystem, so physics-updated Transforms propagate to WorldTransform the
 * same frame. All work runs in fixedUpdate() against ctx.fixedDeltaTime; update()
 * is a no-op.
 *
 * Bodies are assumed to be hierarchy roots, so an entity's local Transform equals
 * its world pose. Parenting a physics body is unsupported in this pass; children
 * parented *to* a body still follow it (markDirty cascades into the subtree).
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
        bool mutatesResources() const override { return false; }

        void setGravity(const glm::vec3& gravity) { m_gravity = gravity; }
        glm::vec3 getGravity() const { return m_gravity; }

        void setSolverIterations(int iterations) { m_solverIterations = iterations; }
        int getSolverIterations() const { return m_solverIterations; }

    private:
        glm::vec3 m_gravity          = {0.0f, -9.81f, 0.0f};
        int       m_solverIterations = 8;        ///< PGS passes per tick

        std::vector<EntityId>        m_bodies;       ///< Live body entities this tick (indexes m_solverBodies)
        std::vector<PhysicsBody>     m_solverBodies; ///< Cached dynamic state, aligned with m_bodies
        std::vector<ContactManifold> m_manifolds;    ///< Reused across ticks; clear() keeps capacity
};

} // namespace Engine
