#pragma once

#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/math/random.h"
#include "core/system.h"
#include "ecs/component/render/particle_emitter.h"
#include "system/particle/live_particles.h"
#include "system/physics/query/query.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief The emitter whose particles fire @p child's ParticleTrigger: its parent, when both carry a
 *        ParticleEmitter.
 *
 * @param scene Scene holding @p child and its parent.
 * @param child An entity with a ParticleTrigger.
 * @return The parent, or a null id when nothing fires it.
 */
EntityId triggeringEmitter(const Scene& scene, EntityId child);

/**
 * @brief Steps every ParticleEmitter's CPU particle simulation.
 *
 * Runs after HierarchySystem in the Transform stage: particles are world-space
 * and never re-based, so an unresolved emitter transform bakes its error into
 * every particle it emits. A colliding emitter's particles ask the physics world's
 * bodies where they are this frame.
 *
 * Publishes FrameContext::particles every frame, paused ones too.
 */
class ParticleSystem : public System {
    public:
        static constexpr float SWIRL_REACH = 1.5f;  ///< The most any axis of swirlAt reaches.

    public:
        ParticleSystem() = default;
        ~ParticleSystem() override = default;

        ParticleSystem(const ParticleSystem& other) = delete;
        ParticleSystem& operator=(const ParticleSystem& other) = delete;

        ParticleSystem(ParticleSystem && other) = delete;
        ParticleSystem& operator=(ParticleSystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

        /**
         * @brief The swirl the turbulence pushes along, at @p position and @p time.
         *
         * Divergence-free: as a flow it neither gathers nor spreads.
         *
         * @param position Where, in units of ParticleEmitter::turbulenceScale.
         * @param time     Seconds of simulation, which drift it.
         * @return The swirl's direction and strength there.
         */
        static glm::vec3 swirlAt(const glm::vec3& position, float time);

    private:
        static constexpr uint64_t SEED = 0x9E3779B97F4A7C15ULL;  ///< What m_rng starts each world from.

        /// Where a sub-emitter's particles are to be born, collected through the walk.
        struct TriggeredSpawn {
            EntityId  emitter;
            glm::vec3 position;
            glm::vec3 velocity;  ///< The parent particle's, scaled by the child's inheritVelocity.
            uint32_t  count;
        };

        /// A sub-emitter under its parent.
        struct ChildLink {
            uint32_t        parentSlot;
            EntityId        child;
            ParticleTrigger trigger;
            float           inheritVelocity;  ///< The child's ParticleEmitter::inheritVelocity.
        };

        /// Where a spawn comes from: a point in the world, the emitter's frame, and what it adds.
        struct Origin {
            glm::vec3 position;
            glm::mat3 frame;     ///< The emitter's world rotation: its shape's axes.
            glm::vec3 velocity;  ///< Added to what the shape sends.
        };

    private:
        /**
         * @brief Bear one particle of @p emitter into @p pool.
         *
         * @param pool    Its pool; nothing is born past the emitter's cap.
         * @param emitter What it is drawn from.
         * @param origin  Where, which way the shape faces, and the velocity it inherits.
         * @param age     Seconds it is born already old, for one born partway through a step.
         * @return Whether it was born.
         */
        bool spawn(
            LiveParticles::Pool& pool,
            const ParticleEmitter& emitter,
            const Origin& origin,
            float age
        );

        /**
         * @brief Queue each sub-emitter of @p emitter that answers @p event, for a particle there.
         *
         * @param emitter  The parent.
         * @param event    What befell the particle.
         * @param particle The parent particle.
         */
        void trigger(EntityId emitter, ParticleEvent event, const Particle& particle);

        /**
         * @brief The sub-emitters of the emitter in @p slot, a run of m_links.
         *
         * @param slot The parent's entity slot.
         * @return The run's first link and one past its last.
         */
        std::pair<const ChildLink*, const ChildLink*> childrenOf(uint32_t slot) const;

        /**
         * @brief Whether no sub-emitter under the emitter in @p slot, however deep, has a particle.
         *
         * @param slot The parent's entity slot.
         * @return True when destroying it would cut no sub-emitter's particle short.
         */
        bool quiet(uint32_t slot) const;

        /**
         * @brief Step @p pool's particles @p dt seconds: forces, collisions, ageing and trails.
         *
         * @param ctx     The frame, whose scene a colliding emitter asks.
         * @param id      The emitter.
         * @param emitter Its settings.
         * @param pool    Its particles.
         * @param dt      Seconds.
         */
        void step(
            FrameContext& ctx,
            EntityId id,
            const ParticleEmitter& emitter,
            LiveParticles::Pool& pool,
            float dt
        );

    private:
        /**
         * @brief The generator every spawn draws from, reseeded with each world.
         *
         * Not Math::Random::rng(), which is per-thread and clock-seeded: a world
         * loaded again draws the same sequence (spawn timing still follows
         * the frame delta).
         */
        Math::Rng m_rng{SEED};

        LiveParticles m_live;
        uint64_t      m_step  = 0;     ///< Stamped into a pool's `seen`.
        uint64_t      m_epoch = 0;     ///< The Scene::epoch() the pools belong to.
        float         m_time  = 0.0f;  ///< Seconds simulated in this world, which drift the swirl.

        std::vector<EntityId> m_spent;  ///< Emitters done with, to destroy after the walk.

        /// Each sub-emitter with an emitter parent, by that parent's slot, gathered each step.
        std::vector<ChildLink> m_links;
        std::vector<TriggeredSpawn> m_triggered;  ///< Spawns the walk queued, born after it.
        QueryParts m_struck;  ///< What a colliding emitter's particles may strike this step.
};

} // namespace Vkm::Engine
