#pragma once

#include "core/math/random.h"
#include "core/system.h"
#include "system/particle/live_particles.h"

namespace Vkm::Engine {

/**
 * @brief Steps every ParticleEmitter's CPU particle simulation.
 *
 * Runs after HierarchySystem in the Transform stage: particles are world-space
 * and never re-based, so an unresolved emitter transform bakes its error into
 * every particle it emits.
 *
 * Publishes FrameContext::particles every frame, paused ones too.
 */
class ParticleSystem : public System {
    public:
        ParticleSystem() = default;
        ~ParticleSystem() override = default;

        ParticleSystem(const ParticleSystem& other) = delete;
        ParticleSystem& operator=(const ParticleSystem& other) = delete;

        ParticleSystem(ParticleSystem && other) = delete;
        ParticleSystem& operator=(ParticleSystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

    private:
        static constexpr uint64_t SEED = 0x9E3779B97F4A7C15ULL;  ///< What m_rng starts each world from.

    private:
        /**
         * @brief The spread generator, reseeded with each world.
         *
         * Not Math::Random::rng(), which is per-thread and clock-seeded: a world
         * loaded again draws the same spread sequence (spawn timing still follows
         * the frame delta).
         */
        Math::Rng m_rng{SEED};

        LiveParticles m_live;
        uint64_t      m_step  = 0;  ///< Stamped into a pool's `seen`.
        uint64_t      m_epoch = 0;  ///< The Scene::epoch() the pools belong to.
};

} // namespace Vkm::Engine
