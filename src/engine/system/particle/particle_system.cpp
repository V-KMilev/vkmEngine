#include "system/particle/particle_system.h"

#include <algorithm>
#include <cmath>

#include "core/clock.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/render/particle_emitter.h"

#include "debug/profiler.h"

namespace Vkm::Engine {

void ParticleSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("ParticleSystem");

    auto& scene = ctx.scene;
    ctx.particles = &m_live;

    // A replaced world reuses slots, so a pool could outlive its emitter into a stranger.
    if (scene.epoch() != m_epoch) {
        m_epoch = scene.epoch();
        m_live.pools.clear();
        m_rng.seed(SEED);
    }

    const float dt = ctx.clock.getSimDelta();
    if (dt <= 0.0f) return;
    ++m_step;

    scene.forEach<ParticleEmitter, Transform>(
        [&](EntityId id, const ParticleEmitter& emitter, const Transform& transform) {
            LiveParticles::Pool& pool = m_live.pools[id.slot()];
            if (pool.emitter != id) {
                pool = LiveParticles::Pool{};
                pool.emitter = id;
            }
            pool.seen = m_step;

            const glm::vec3 origin = resolvedWorldPosition(scene, id, transform);

            for (Particle& p : pool.particles) {
                p.age      += dt;
                p.velocity += emitter.acceleration * dt;
                p.position += p.velocity * dt;
            }

            pool.particles.erase(
                std::remove_if(
                    pool.particles.begin(),
                    pool.particles.end(),
                    [](const Particle& p) { return p.age >= p.lifetime; }
                ),
                pool.particles.end()
            );

            if (!emitter.emitting || emitter.rate <= 0.0f) return;

            pool.spawnAccumulator += emitter.rate * dt;
            while (pool.spawnAccumulator >= 1.0f) {
                // Only the fraction survives at capacity, or a saturated emitter
                // discharges every banked spawn the instant particles die.
                if (pool.particles.size() >= emitter.maxParticles) {
                    pool.spawnAccumulator -= std::floor(pool.spawnAccumulator);
                    break;
                }
                pool.spawnAccumulator -= 1.0f;

                Particle p;
                p.position = origin;
                p.velocity = emitter.velocity + glm::vec3(
                    m_rng.nextFloat(-emitter.spread, emitter.spread),
                    m_rng.nextFloat(-emitter.spread, emitter.spread),
                    m_rng.nextFloat(-emitter.spread, emitter.spread)
                );
                p.age      = 0.0f;
                p.lifetime = emitter.lifetime;
                pool.particles.push_back(p);
            }
        }
    );

    // An emitter removed or destroyed since leaves a pool nothing reads.
    for (auto it = m_live.pools.begin(); it != m_live.pools.end();) {
        if (it->second.seen != m_step) it = m_live.pools.erase(it);
        else                            ++it;
    }
}

} // namespace Vkm::Engine
