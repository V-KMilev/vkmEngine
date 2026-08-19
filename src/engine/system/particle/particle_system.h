#pragma once

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
};

} // namespace Vkm::Engine
