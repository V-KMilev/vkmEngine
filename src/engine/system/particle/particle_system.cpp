#include "system/particle/particle_system.h"

#include <algorithm>
#include <cmath>

#include "core/clock.h"
#include "ecs/hierarchy_operations.h"
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

    m_spent.clear();
    scene.forEach<ParticleEmitter, Transform>(
        [&](EntityId id, ParticleEmitter& emitter, const Transform& transform) {
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

            const auto spawn = [&]() {
                Particle p;
                p.position = origin + glm::vec3(
                    m_rng.nextFloat(-emitter.halfExtents.x, emitter.halfExtents.x),
                    m_rng.nextFloat(-emitter.halfExtents.y, emitter.halfExtents.y),
                    m_rng.nextFloat(-emitter.halfExtents.z, emitter.halfExtents.z)
                );
                p.velocity = emitter.velocity + glm::vec3(
                    m_rng.nextFloat(-emitter.spread, emitter.spread),
                    m_rng.nextFloat(-emitter.spread, emitter.spread),
                    m_rng.nextFloat(-emitter.spread, emitter.spread)
                );
                p.age      = 0.0f;
                p.lifetime = emitter.lifetime;
                pool.particles.push_back(p);
                pool.fired = true;
            };

            // Spent whole, as far as the cap allows: a burst is one moment, not a debt.
            const size_t live = pool.particles.size();
            const size_t room = emitter.maxParticles > live ? emitter.maxParticles - live : 0;
            for (size_t i = 0; i < std::min<size_t>(emitter.burst, room); ++i) spawn();
            emitter.burst = 0;

            if (emitter.emitting && emitter.rate > 0.0f) {
                pool.spawnAccumulator += emitter.rate * dt;
                while (pool.spawnAccumulator >= 1.0f) {
                    // Only the fraction survives at capacity, or a saturated emitter
                    // discharges every banked spawn the instant particles die.
                    if (pool.particles.size() >= emitter.maxParticles) {
                        pool.spawnAccumulator -= std::floor(pool.spawnAccumulator);
                        break;
                    }
                    pool.spawnAccumulator -= 1.0f;
                    spawn();
                }
            }

            // Never fired is not spent: an emitter waiting for gameplay's burst stays.
            const bool spent = pool.fired && !emitter.emitting && pool.particles.empty();
            if (emitter.destroyWhenSpent && spent) m_spent.push_back(id);
        }
    );

    // Outside the walk, which a destroy would disturb.
    for (const EntityId id : m_spent) HierarchyOperations::destroyHierarchy(scene, id);

    // An emitter removed or destroyed since leaves a pool nothing reads.
    for (auto it = m_live.pools.begin(); it != m_live.pools.end();) {
        if (it->second.seen != m_step) it = m_live.pools.erase(it);
        else                            ++it;
    }
}

} // namespace Vkm::Engine
