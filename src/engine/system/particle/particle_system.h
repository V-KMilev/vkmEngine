#pragma once

#include "core/math/random.h"
#include "core/system.h"

namespace Vkm::Engine {

/**
 * @brief Steps every ParticleEmitter's CPU particle simulation.
 *
 * Registered at SystemStage::Simulation. Deliberately CPU-side: the counts an
 * FPS needs (muzzle flashes, impacts) are small, and it keeps the emitter
 * authorable as plain data.
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
        /**
         * @brief The spread generator, seeded once and owned by the system.
         *
         * Math::Random::rng() is per-thread and clock-seeded, so a spawn drawn
         * from it depends on when the process started and which worker ran the
         * emitter. Owning one makes the same scene played twice produce the
         * same effect.
         */
        Math::Rng m_rng{0x9E3779B97F4A7C15ULL};
};

} // namespace Vkm::Engine
