#include "system/particle/particle_system.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/clock.h"
#include "core/math/rotation.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "system/physics/query/query.h"

#include "debug/profiler.h"

namespace Vkm::Engine {

namespace {

/// A direction drawn evenly over the sphere.
glm::vec3 anyDirection(Math::Rng& rng) {
    const float z   = rng.nextFloat(-1.0f, 1.0f);
    const float phi = rng.nextFloat(0.0f, glm::two_pi<float>());
    const float r   = std::sqrt(std::max(1.0f - z * z, 0.0f));
    return {r * std::cos(phi), z, r * std::sin(phi)};
}

/// A point drawn evenly over a disc of @p radius in the XZ plane, or on its rim.
glm::vec3 discPoint(Math::Rng& rng, float radius, bool rim) {
    const float r   = rim ? radius : radius * std::sqrt(rng.nextFloat());
    const float phi = rng.nextFloat(0.0f, glm::two_pi<float>());
    return {r * std::cos(phi), 0.0f, r * std::sin(phi)};
}

/// A point drawn in a box of @p half, or on its surface, each face by its area.
glm::vec3 boxPoint(Math::Rng& rng, const glm::vec3& half, bool surface) {
    glm::vec3 p(
        rng.nextFloat(-half.x, half.x),
        rng.nextFloat(-half.y, half.y),
        rng.nextFloat(-half.z, half.z)
    );
    if (!surface) return p;
    const glm::vec3 area(half.y * half.z, half.x * half.z, half.x * half.y);
    const float     pick = rng.nextFloat(0.0f, area.x + area.y + area.z);
    const int       axis = pick < area.x ? 0 : pick < area.x + area.y ? 1 : 2;
    p[axis] = rng.nextBool() ? half[axis] : -half[axis];
    return p;
}

/// A direction drawn evenly over the cap of those within @p angle of +Y; none draws at an angle of 0.
glm::vec3 withinAngleOfUp(Math::Rng& rng, float angle) {
    if (angle <= 0.0f) return {0.0f, 1.0f, 0.0f};
    const float cosAngle = std::cos(std::min(angle, glm::pi<float>()));
    const float y        = rng.nextFloat(cosAngle, 1.0f);
    const float phi      = rng.nextFloat(0.0f, glm::two_pi<float>());
    const float r        = std::sqrt(std::max(1.0f - y * y, 0.0f));
    return {r * std::cos(phi), y, r * std::sin(phi)};
}

/// Where a particle of @p emitter is born and which way it leaves, in the emitter's frame.
void shapeOf(const ParticleEmitter& emitter, Math::Rng& rng, glm::vec3& point, glm::vec3& direction) {
    const float radius = std::max(emitter.radius, 0.0f);
    switch (emitter.shape) {
        case EmitShape::Point:
            point     = glm::vec3(0.0f);
            direction = anyDirection(rng);
            return;
        case EmitShape::Sphere:
        case EmitShape::Hemisphere: {
            direction = anyDirection(rng);
            if (emitter.shape == EmitShape::Hemisphere) direction.y = std::abs(direction.y);
            const float r = emitter.fromSurface ? radius : radius * std::cbrt(rng.nextFloat());
            point = direction * r;
            return;
        }
        case EmitShape::Cone:
            point     = discPoint(rng, radius, emitter.fromSurface);
            direction = withinAngleOfUp(rng, emitter.angle);
            return;
        case EmitShape::Box:
            point     = boxPoint(rng, glm::max(emitter.halfExtents, glm::vec3(0.0f)), emitter.fromSurface);
            direction = withinAngleOfUp(rng, emitter.angle);
            return;
        case EmitShape::Disc: {
            point = discPoint(rng, radius, emitter.fromSurface);
            const float out = glm::length(point);
            direction = out > 1e-6f ? point / out : discPoint(rng, 1.0f, true);
            return;
        }
        case EmitShape::Count:
            break;
    }
    point     = glm::vec3(0.0f);
    direction = {0.0f, 1.0f, 0.0f};
}

/// The rotation in @p model, its scale taken out.
glm::mat3 rotationOf(const glm::mat4& model) { return glm::mat3_cast(Math::worldRotationOf(model)); }

/// How far a struck particle is set off the surface, metres.
constexpr float BACK_OFF = 1e-3f;

/// A rate as spawned: none for a negative or non-finite one, which would count no whole particles.
float rateOf(float rate) { return std::isfinite(rate) ? std::max(rate, 0.0f) : 0.0f; }

} // namespace

EntityId triggeringEmitter(const Scene& scene, EntityId child) {
    const Hierarchy* at = scene.tryGet<Hierarchy>(child);
    const bool fired = scene.has<ParticleEmitter>(child) && at && at->parent
        && scene.has<ParticleEmitter>(at->parent);
    return fired ? at->parent : EntityId{};
}

glm::vec3 ParticleSystem::swirlAt(const glm::vec3& position, float time) {
    // Each axis reads only the other two, so the field has no divergence.
    const glm::vec3& q = position;
    return {
        std::sin(q.y + 1.3f * time) + 0.5f * std::sin(1.9f * q.z - 0.7f * time),
        std::sin(q.z + 0.9f * time) + 0.5f * std::sin(2.1f * q.x + 1.1f * time),
        std::sin(q.x - 1.1f * time) + 0.5f * std::sin(1.7f * q.y + 0.5f * time),
    };
}

bool ParticleSystem::spawn(
    LiveParticles::Pool& pool,
    const ParticleEmitter& emitter,
    const Origin& origin,
    float age
) {
    if (pool.particles.size() >= emitter.maxParticles) return false;

    glm::vec3 point;
    glm::vec3 direction;
    shapeOf(emitter, m_rng, point, direction);

    Particle p;
    p.position = origin.position + origin.frame * point;
    p.velocity = origin.frame * direction * emitter.speed.draw(m_rng) + origin.velocity;
    p.lifetime = std::max(emitter.lifetime.draw(m_rng), 1e-3f);
    p.size     = std::max(emitter.size.draw(m_rng), 0.0f);
    p.angle    = emitter.rotation.draw(m_rng);
    p.spin     = emitter.spin.draw(m_rng);
    // Drawn only when asked for: an emitter without it draws the same sequence regardless.
    if (emitter.randomFrame) {
        p.frame = static_cast<uint32_t>(m_rng.nextInt(0, static_cast<int>(sheetFrames(emitter)) - 1));
    }
    p.age = std::min(age, p.lifetime * 0.5f);
    // Moved on by its age, unless that could carry it through a body no ray has asked about.
    if (!emitter.collide) p.position += p.velocity * p.age;

    pool.particles.push_back(p);
    if (emitter.render == ParticleRender::Trail) {
        pool.trails.resize(pool.particles.size() * LiveParticles::TRAIL_POINTS, p.position);
    }
    pool.fired = true;
    trigger(pool.emitter, ParticleEvent::Birth, p);
    return true;
}

std::pair<const ParticleSystem::ChildLink*, const ParticleSystem::ChildLink*>
ParticleSystem::childrenOf(uint32_t slot) const {
    const auto bySlot = [](const ChildLink& link, uint32_t at) { return link.parentSlot < at; };
    const auto first  = std::lower_bound(m_links.begin(), m_links.end(), slot, bySlot);
    auto       last   = first;
    while (last != m_links.end() && last->parentSlot == slot) ++last;
    return {m_links.data() + (first - m_links.begin()), m_links.data() + (last - m_links.begin())};
}

void ParticleSystem::trigger(EntityId emitter, ParticleEvent event, const Particle& particle) {
    if (m_links.empty()) return;
    const auto [first, last] = childrenOf(emitter.slot());
    for (const ChildLink* link = first; link != last; ++link) {
        if (link->trigger.event != event || link->trigger.count == 0) continue;
        const glm::vec3 inherited = particle.velocity * link->inheritVelocity;
        m_triggered.push_back({link->child, particle.position, inherited, link->trigger.count});
    }
}

bool ParticleSystem::quiet(uint32_t slot) const {
    const auto [first, last] = childrenOf(slot);
    for (const ChildLink* link = first; link != last; ++link) {
        const LiveParticles::Pool* pool = m_live.poolOf(link->child);
        if ((pool && !pool->particles.empty()) || !quiet(link->child.slot())) return false;
    }
    return true;
}

void ParticleSystem::step(
    FrameContext& ctx,
    EntityId id,
    const ParticleEmitter& emitter,
    LiveParticles::Pool& pool,
    float dt
) {
    constexpr uint32_t POINTS = LiveParticles::TRAIL_POINTS;
    const bool  trails     = emitter.render == ParticleRender::Trail;
    const float keep       = std::exp(-std::max(emitter.drag, 0.0f) * dt);
    const float scale      = std::max(emitter.turbulenceScale, 1e-3f);
    const float trailEvery = std::max(emitter.trailLength, 1e-3f) / static_cast<float>(POINTS);
    // Gathered once for every ray of the step, around where they run: as far along each axis as the
    // fastest particle can go, with the pull and the swirl on it.
    if (emitter.collide && !pool.particles.empty()) {
        glm::vec3  reach(0.0f);
        Math::AABB around{pool.particles.front().position, pool.particles.front().position};
        for (const Particle& p : pool.particles) {
            around.min = glm::min(around.min, p.position);
            around.max = glm::max(around.max, p.position);
            reach      = glm::max(reach, glm::abs(p.velocity));
        }
        const float     swirl = SWIRL_REACH * std::max(emitter.turbulence, 0.0f);
        const glm::vec3 pull  = glm::abs(emitter.acceleration) + swirl;
        const glm::vec3 grow  = (reach + pull * dt) * dt + BACK_OFF;
        QueryFilter filter;
        filter.ignore = id;
        m_struck.gather(ctx.scene, filter, {around.min - grow, around.max + grow});
    }

    // An emitter turned to trails keeps none yet: each particle's starts where it is.
    if (trails && pool.trails.size() != pool.particles.size() * POINTS) {
        pool.trails.assign(pool.particles.size() * POINTS, glm::vec3(0.0f));
        for (Particle& p : pool.particles) p.trailCount = 0;
    }

    size_t kept = 0;
    for (size_t i = 0; i < pool.particles.size(); ++i) {
        Particle p = pool.particles[i];
        glm::vec3* trail = trails ? pool.trails.data() + i * POINTS : nullptr;

        glm::vec3 acceleration = emitter.acceleration;
        if (emitter.turbulence > 0.0f) {
            acceleration += emitter.turbulence * swirlAt(p.position / scale, m_time);
        }
        p.velocity = (p.velocity + acceleration * dt) * keep;

        const glm::vec3 from = p.position;
        p.position += p.velocity * dt;
        p.age      += dt;

        const glm::vec3 travel = p.position - from;
        RayHit          hit;
        // Not from inside a body: one born on or in it leaves it rather than being held there.
        const bool struck = emitter.collide && glm::dot(travel, travel) > 1e-12f
            && m_struck.raycast(from, travel, glm::length(travel), hit) && hit.distance > 0.0f;
        if (struck) {
            // A contact only when it came in faster than settling brings it: one resting on a
            // body falls the back-off and strikes it again, at any step rate, and counts none.
            const float into    = glm::dot(p.velocity, hit.normal);
            const float pull    = std::max(-glm::dot(acceleration, hit.normal), 0.0f);
            const float settled = 2.0f * (std::sqrt(2.0f * pull * BACK_OFF) + pull * dt);
            const bool  counts  = -into > settled;

            // Back off the surface, and turn what ran into it back out, as much as bounce keeps.
            p.position = hit.point + hit.normal * BACK_OFF;
            if (into < 0.0f) p.velocity -= (1.0f + std::max(emitter.bounce, 0.0f)) * into * hit.normal;
            if (counts) {
                p.age += std::max(emitter.lifeLostOnHit, 0.0f) * p.lifetime;
                trigger(id, ParticleEvent::Collision, p);
            }
        }

        if (p.age >= p.lifetime) {
            trigger(id, ParticleEvent::Death, p);
            continue;
        }

        if (!trails) {
            p.trailCount = 0;
        } else {
            p.trailClock += dt;
            if (p.trailClock >= trailEvery) {
                p.trailClock = std::fmod(p.trailClock, trailEvery);
                // Where it was before this step, so its newest point is behind it, never on it.
                std::copy_backward(trail, trail + POINTS - 1, trail + POINTS);
                trail[0]     = from;
                p.trailCount = std::min(p.trailCount + 1, POINTS);
            }
        }

        // Kept in birth order, a ribbon's and a trail's along with it.
        if (kept != i && trails) std::copy(trail, trail + POINTS, pool.trails.data() + kept * POINTS);
        pool.particles[kept++] = p;
    }
    pool.particles.resize(kept);
    pool.trails.resize(trails ? kept * POINTS : 0);
}

void ParticleSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("ParticleSystem");

    auto& scene = ctx.scene;
    ctx.particles = &m_live;

    // A replaced world reuses slots, so a pool could outlive its emitter into a stranger.
    if (scene.epoch() != m_epoch) {
        m_epoch = scene.epoch();
        m_live.pools.clear();
        m_rng.seed(SEED);
        m_time = 0.0f;
    }

    const float dt = ctx.clock.getSimDelta();
    if (dt <= 0.0f) {
        // An emitter moved while nothing steps - dragged in a paused editor - has not travelled.
        for (auto& [slot, pool] : m_live.pools) pool.placed = false;
        return;
    }
    ++m_step;
    m_time += dt;

    // Which emitters have sub-emitters, and on what, by the parent's slot.
    m_links.clear();
    scene.forEach<ParticleTrigger>([&](EntityId child, const ParticleTrigger& trigger) {
        const EntityId parent = triggeringEmitter(scene, child);
        if (!parent) return;
        const float inherit = scene.get<ParticleEmitter>(child).inheritVelocity;
        m_links.push_back({parent.slot(), child, trigger, inherit});
    });
    // By parent, then child: a parent's sub-emitters in one order everywhere, which the stream follows.
    const auto byParent = [](const ChildLink& a, const ChildLink& b) {
        return a.parentSlot != b.parentSlot ? a.parentSlot < b.parentSlot : a.child.slot() < b.child.slot();
    };
    std::sort(m_links.begin(), m_links.end(), byParent);

    m_spent.clear();
    m_triggered.clear();
    scene.forEach<ParticleEmitter, Transform>(
        [&](EntityId id, ParticleEmitter& emitter, const Transform& transform) {
            LiveParticles::Pool& pool = m_live.pools[id.slot()];
            if (pool.emitter != id) {
                pool = LiveParticles::Pool{};
                pool.emitter = id;
            }
            pool.seen = m_step;

            const glm::mat4 model = resolvedWorldMatrix(scene, id, transform);
            const glm::vec3 here  = glm::vec3(model[3]);
            const glm::vec3 moved = pool.placed ? here - pool.lastOrigin : glm::vec3(0.0f);
            const glm::vec3 from  = pool.placed ? pool.lastOrigin : here;
            const Origin    origin{here, rotationOf(model), moved / dt * emitter.inheritVelocity};

            step(ctx, id, emitter, pool, dt);
            pool.lastOrigin = here;
            pool.placed     = true;

            // A sub-emitter spawns only when its parent's particles trigger it, and never is spent.
            if (scene.has<ParticleTrigger>(id)) {
                emitter.burst = 0;
                return;
            }

            // Spent whole, as far as the cap allows: a burst is one moment, not a debt. Fired even
            // when the cap lets none through, or a puff of no room would wait forever.
            pool.fired = pool.fired || emitter.burst > 0;
            for (uint32_t i = 0; i < emitter.burst && spawn(pool, emitter, origin, 0.0f); ++i) {}
            emitter.burst = 0;

            if (emitter.emitting) {
                pool.spawnAccumulator += rateOf(emitter.rate) * dt;
                pool.metreAccumulator += rateOf(emitter.ratePerMetre) * glm::length(moved);
                const auto byTime     = static_cast<uint32_t>(pool.spawnAccumulator);
                const auto byDistance = static_cast<uint32_t>(pool.metreAccumulator);
                pool.spawnAccumulator -= static_cast<float>(byTime);
                pool.metreAccumulator -= static_cast<float>(byDistance);

                // Spread along the step: each is born where the emitter was when it fell due, as
                // old as the time since. Only the fraction survives at capacity, or a saturated
                // emitter would discharge every banked spawn the instant particles die.
                const uint32_t due = byTime + byDistance;
                for (uint32_t i = 0; i < due; ++i) {
                    const float share = (static_cast<float>(i) + 1.0f) / static_cast<float>(due);
                    Origin along = origin;
                    along.position = glm::mix(from, here, share);
                    if (!spawn(pool, emitter, along, (1.0f - share) * dt)) break;
                }
            }

            // Never fired is not spent: an emitter waiting for gameplay's burst stays.
            const bool spent = pool.fired && !emitter.emitting && pool.particles.empty();
            if (emitter.destroyWhenSpent && spent) m_spent.push_back(id);
        }
    );

    // Born after the walk, which a sub-emitter's own step has passed: they start next frame. By
    // index, for a birth here may queue its own children's.
    EntityId  framed;
    glm::mat3 frame(1.0f);
    for (size_t q = 0; q < m_triggered.size(); ++q) {
        const TriggeredSpawn queued = m_triggered[q];
        const ParticleEmitter* emitter = scene.tryGet<ParticleEmitter>(queued.emitter);
        const Transform*       local   = scene.tryGet<Transform>(queued.emitter);
        const auto             pool    = m_live.pools.find(queued.emitter.slot());
        const bool live = pool != m_live.pools.end() && pool->second.emitter == queued.emitter;
        if (!emitter || !local || !live) continue;
        // A burst of deaths queues one child many times in a row: its frame is worked out once.
        if (queued.emitter != framed) {
            framed = queued.emitter;
            frame  = rotationOf(resolvedWorldMatrix(scene, queued.emitter, *local));
        }
        const Origin origin{queued.position, frame, queued.velocity};
        for (uint32_t i = 0; i < queued.count && spawn(pool->second, *emitter, origin, 0.0f); ++i) {}
    }

    // Outside the walk, which a destroy would disturb; once its sub-emitters' particles are gone
    // too, for they go with it.
    for (const EntityId id : m_spent) {
        if (quiet(id.slot())) HierarchyOperations::destroyHierarchy(scene, id);
    }

    // An emitter removed or destroyed since leaves a pool nothing reads.
    for (auto it = m_live.pools.begin(); it != m_live.pools.end();) {
        if (it->second.seen != m_step) it = m_live.pools.erase(it);
        else                            ++it;
    }
}

} // namespace Vkm::Engine
