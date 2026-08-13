#pragma once

#include "core/system.h"

namespace Engine {

/**
 * @brief Steps every ParticleEmitter's CPU particle simulation.
 *
 * Registered at SystemStage::Simulation. Each update ages and integrates the live
 * particles, retires the expired ones, and spawns new ones at the emitter's rate
 * from its current world position. Deliberately CPU-side: the counts an FPS needs
 * (muzzle flashes, impacts) are small, and it keeps the emitter authorable as
 * plain data.
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

} // namespace Engine
