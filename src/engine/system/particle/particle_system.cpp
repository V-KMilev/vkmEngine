#include "system/particle/particle_system.h"

#include <algorithm>
#include <cmath>

#include "core/clock.h"
#include "core/math/random.h"
#include "ecs/scene.h"
#include "ecs/component/particle_emitter.h"
#include "ecs/component/transform.h"
#include "ecs/component/world_transform.h"

#include "debug/profiler.h"

namespace Engine {

void ParticleSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("ParticleSystem::update");

    auto&       scene = ctx.scene;
    const float dt    = ctx.clock.getSimDelta();
    if (dt <= 0.0f) return;

    scene.forEach<ParticleEmitter, Transform>(
        [&](EntityId id, ParticleEmitter& emitter, const Transform& transform) {
            // Spawn origin: the emitter's world position (parented or not).
            glm::vec3 origin = transform.position;
            if (scene.has<WorldTransform>(id)) {
                origin = glm::vec3(scene.get<WorldTransform>(id).model[3]);
            }

            // Age + integrate the live particles.
            for (Particle& p : emitter.particles) {
                p.age      += dt;
                p.velocity += emitter.acceleration * dt;
                p.position += p.velocity * dt;
            }

            // Retire the expired ones.
            emitter.particles.erase(
                std::remove_if(emitter.particles.begin(), emitter.particles.end(),
                               [](const Particle& p) { return p.age >= p.lifetime; }),
                emitter.particles.end());

            if (!emitter.emitting || emitter.rate <= 0.0f) return;

            // Spawn at the authored rate, capped. The accumulator carries the
            // fractional remainder so a low rate still emits evenly.
            emitter.spawnAccumulator += emitter.rate * dt;
            while (emitter.spawnAccumulator >= 1.0f) {
                // At capacity the accumulator keeps only its fraction. Banking
                // whole spawns while full meant a long-saturated emitter built
                // up arbitrarily many credits and then discharged them the
                // instant particles started dying - a visible burst out of
                // nowhere, worse the longer it had been full.
                if (emitter.particles.size() >= emitter.maxParticles) {
                    emitter.spawnAccumulator -= std::floor(emitter.spawnAccumulator);
                    break;
                }
                emitter.spawnAccumulator -= 1.0f;

                Particle p;
                p.position = origin;
                p.velocity = emitter.velocity + glm::vec3(
                    Math::Random::range(-emitter.spread, emitter.spread),
                    Math::Random::range(-emitter.spread, emitter.spread),
                    Math::Random::range(-emitter.spread, emitter.spread));
                p.age      = 0.0f;
                p.lifetime = emitter.lifetime;
                emitter.particles.push_back(p);
            }
        });
}

} // namespace Engine
