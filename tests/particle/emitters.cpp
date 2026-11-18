// Particle emitters as a game runs them: how many they bear, where and which way, how they move and
// die, and the same every run.

#include <cmath>
#include <functional>
#include <vector>

#include "core/frame/live_particles.h"
#include "ecs/component/effect/particle_emitter.h"
#include "ecs/component/scene/transform.h"
#include "ecs/hierarchy_operations.h"

#include "harness/check.h"
#include "harness/watch.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

/// An emitter at @p at, firing nothing until told: rates zero, a long life, no pull.
EntityId addEmitter(
    Scene& scene,
    const glm::vec3& at = glm::vec3(0.0f),
    const glm::quat& turn = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.position = at;
    pose.rotation = turn;
    scene.add(id, pose);
    ParticleEmitter emitter;
    emitter.rate         = 0.0f;
    emitter.lifetime     = {10.0f, 10.0f};
    emitter.speed        = {1.0f, 1.0f};
    emitter.acceleration = glm::vec3(0.0f);
    emitter.maxParticles = 1000;
    scene.add(id, emitter);
    return id;
}

const std::vector<Particle>& particlesOf(const Watch& seen, EntityId emitter) {
    static const std::vector<Particle> none;
    const LiveParticles::Pool* pool = seen.frame->particles ? seen.frame->particles->poolOf(
        emitter
    ) : nullptr;
    return pool ? pool->particles : none;
}

/// Where @p p was born: it has flown straight since, at its speed.
glm::vec3 birthOf(const Particle& p) { return p.position - p.velocity * p.age; }

} // namespace

TEST("an emitter bears its rate a second, its rate a meter it travels and its bursts, never past its cap") {
    Game game;
    Scene& scene = game.scene();
    const EntityId timed = addEmitter(scene);
    scene.get<ParticleEmitter>(timed).rate = 30.0f;
    const EntityId trailing = addEmitter(scene, {0.0f, 0.0f, 5.0f});
    scene.get<ParticleEmitter>(trailing).ratePerMeter = 4.0f;
    const EntityId burst = addEmitter(scene, {0.0f, 0.0f, 10.0f});
    scene.get<ParticleEmitter>(burst).emitting = false;
    scene.get<ParticleEmitter>(burst).burst    = 50;
    const EntityId capped = addEmitter(scene, {0.0f, 0.0f, 15.0f});
    scene.get<ParticleEmitter>(capped).rate         = 1000.0f;
    scene.get<ParticleEmitter>(capped).maxParticles = 100;

    Watch& seen = watch(game);
    size_t burstAfterOne = 0;
    game.run(60, [&](int frame) {
        // The trailing emitter moves 10 cm a frame: 6 m in the second.
        scene.get<Transform>(trailing).position.x += 0.1f;
        if (frame == 1) burstAfterOne = particlesOf(seen, burst).size();
    });

    CHECK(particlesOf(seen, timed).size() == 30);
    CHECK(particlesOf(seen, trailing).size() >= 23);
    CHECK(particlesOf(seen, trailing).size() <= 24);
    CHECK(burstAfterOne == 50);
    CHECK(particlesOf(seen, burst).size() == 50);
    CHECK(scene.get<ParticleEmitter>(burst).burst == 0);
    CHECK(particlesOf(seen, capped).size() == 100);
}

TEST("a particle moves under its acceleration and drag, and dies at the end of its life") {
    Game game;
    Scene& scene = game.scene();
    auto fire = [&](const glm::vec3& at, auto&& tune) {
        const EntityId id = addEmitter(scene, at);
        ParticleEmitter& emitter = scene.get<ParticleEmitter>(id);
        emitter.emitting = false;
        emitter.burst    = 1;
        emitter.shape    = EmitShape::Cone;
        emitter.angle    = 0.0f;
        tune(emitter);
        return id;
    };
    const EntityId falling = fire({0.0f, 0.0f, 0.0f}, [](ParticleEmitter& e) {
        e.speed        = {0.0f, 0.0f};
        e.acceleration = {0.0f, -2.0f, 0.0f};
    });
    const EntityId dragged = fire({5.0f, 0.0f, 0.0f}, [](ParticleEmitter& e) {
        e.speed = {4.0f, 4.0f};
        e.drag  = 1.0f;
    });
    const EntityId brief = fire({10.0f, 0.0f, 0.0f}, [](ParticleEmitter& e) { e.lifetime = {0.5f, 0.5f}; });

    Watch& seen = watch(game);
    size_t briefAt29 = 0;
    game.run(61, [&](int frame) {
        if (frame == 30) briefAt29 = particlesOf(seen, brief).size();
    });

    // A second after it was born (the frame after the burst).
    REQUIRE(particlesOf(seen, falling).size() == 1);
    const Particle& drop = particlesOf(seen, falling)[0];
    CHECK_NEAR(drop.age, 1.0f, 1e-4f);
    CHECK_NEAR(drop.velocity, glm::vec3(0.0f, -2.0f, 0.0f), 1e-3f);
    CHECK_NEAR(drop.position.y, -1.0f, 0.03f);
    REQUIRE(particlesOf(seen, dragged).size() == 1);
    // It keeps exp(-1) of its speed a second.
    CHECK_NEAR(particlesOf(seen, dragged)[0].velocity, glm::vec3(0.0f, 4.0f * std::exp(-1.0f), 0.0f), 0.02f);
    CHECK(briefAt29 == 1);
    CHECK(particlesOf(seen, brief).empty());
}

TEST("each emission shape bears its particles inside it or on its surface, leaving the way it says") {
    Game game;
    Scene& scene = game.scene();
    std::vector<std::function<void(const Particle&)>> checks;
    auto shape = [&](
        EmitShape kind,
        bool surface,
        auto&& tune,
        const glm::quat& turn = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
    ) {
        const EntityId id = addEmitter(scene, glm::vec3(0.0f), turn);
        ParticleEmitter& emitter = scene.get<ParticleEmitter>(id);
        emitter.emitting    = false;
        emitter.burst       = 200;
        emitter.shape       = kind;
        emitter.fromSurface = surface;
        emitter.radius      = 2.0f;
        emitter.angle       = glm::radians(30.0f);
        emitter.halfExtents = {1.0f, 2.0f, 3.0f};
        tune(emitter);
        return id;
    };
    const auto none = [](ParticleEmitter&) {};
    const EntityId ball    = shape(EmitShape::Sphere, false, none);
    const EntityId shell   = shape(EmitShape::Sphere, true, none);
    const EntityId dome    = shape(EmitShape::Hemisphere, false, none);
    const EntityId cone    = shape(EmitShape::Cone, false, none);
    const EntityId rim     = shape(EmitShape::Cone, true, none);
    const EntityId box     = shape(EmitShape::Box, false, none);
    const EntityId boxSkin = shape(EmitShape::Box, true, none);
    const EntityId disc    = shape(EmitShape::Disc, false, none);
    const EntityId point   = shape(EmitShape::Point, false, none);
    // A cone turned to point along +Z.
    const glm::quat forward = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const EntityId turned   = shape(EmitShape::Cone, false, none, forward);

    Watch& seen = watch(game);
    game.run(2);

    const float slack   = 1e-4f;
    const float cosCone = std::cos(glm::radians(30.0f));
    const auto each = [&](EntityId id, auto&& check) {
        const std::vector<Particle>& born = particlesOf(seen, id);
        CHECK(born.size() == 200);
        for (const Particle& p : born) check(birthOf(p), glm::normalize(p.velocity));
    };
    each(ball, [&](glm::vec3 at, glm::vec3 way) {
        CHECK(glm::length(at) <= 2.0f + slack);
        if (glm::length(at) > 0.01f) CHECK(glm::dot(way, glm::normalize(at)) > 0.999f);
    });
    each(shell, [&](glm::vec3 at, glm::vec3) { CHECK_NEAR(glm::length(at), 2.0f, 1e-3f); });
    each(dome, [&](glm::vec3 at, glm::vec3 way) {
        CHECK(at.y >= -slack);
        CHECK(glm::length(at) <= 2.0f + slack);
        CHECK(way.y >= -slack);
    });
    each(cone, [&](glm::vec3 at, glm::vec3 way) {
        CHECK(std::fabs(at.y) < slack);
        CHECK(glm::length(glm::vec2(at.x, at.z)) <= 2.0f + slack);
        CHECK(way.y >= cosCone - slack);
    });
    each(rim, [&](glm::vec3 at, glm::vec3) { CHECK_NEAR(glm::length(glm::vec2(at.x, at.z)), 2.0f, 1e-3f); });
    each(box, [&](glm::vec3 at, glm::vec3 way) {
        CHECK(glm::all(glm::lessThanEqual(glm::abs(at), glm::vec3(1.0f, 2.0f, 3.0f) + slack)));
        CHECK(way.y >= cosCone - slack);
    });
    each(boxSkin, [&](glm::vec3 at, glm::vec3) {
        const glm::vec3 out = glm::abs(at) / glm::vec3(1.0f, 2.0f, 3.0f);
        CHECK_NEAR(std::max({out.x, out.y, out.z}), 1.0f, 1e-3f);
    });
    each(disc, [&](glm::vec3 at, glm::vec3 way) {
        CHECK(std::fabs(at.y) < slack);
        CHECK(glm::length(at) <= 2.0f + slack);
        CHECK(std::fabs(way.y) < slack);
        if (glm::length(at) > 0.01f) CHECK(glm::dot(way, glm::normalize(at)) > 0.999f);
    });
    int down = 0;
    each(point, [&](glm::vec3 at, glm::vec3 way) {
        CHECK(glm::length(at) < slack);
        down += way.y < 0.0f;
    });
    CHECK(down > 50);
    each(turned, [&](glm::vec3 at, glm::vec3 way) {
        CHECK(std::fabs(at.z) < slack);
        CHECK(way.z >= cosCone - slack);
    });
}

TEST("the same emitters run twice bear the same particles, bit for bit") {
    auto run = [](std::vector<Particle>& out) {
        Game game;
        const EntityId id = addEmitter(game.scene());
        ParticleEmitter& emitter = game.scene().get<ParticleEmitter>(id);
        emitter.rate       = 50.0f;
        emitter.shape      = EmitShape::Sphere;
        emitter.radius     = 1.0f;
        emitter.speed      = {1.0f, 3.0f};
        emitter.size       = {0.1f, 0.4f};
        emitter.spin       = {-2.0f, 2.0f};
        emitter.lifetime   = {0.5f, 2.0f};
        emitter.turbulence = 3.0f;
        Watch& seen = watch(game);
        game.run(90);
        out = particlesOf(seen, id);
    };
    std::vector<Particle> first, second;
    run(first);
    run(second);

    REQUIRE(first.size() == second.size());
    REQUIRE(!first.empty());
    bool same = true;
    for (size_t i = 0; i < first.size(); ++i) {
        same = same && first[i].position == second[i].position && first[i].velocity == second[i].velocity
            && first[i].size == second[i].size && first[i].lifetime == second[i].lifetime
            && first[i].spin == second[i].spin;
    }
    CHECK(same);
    // Not all alike: the ranges were drawn from.
    CHECK(first.front().size != first.back().size);
}

TEST("a one-off effect goes, with everything under it, once its last particle dies, and one never fired stays") {
    Game game;
    Scene& scene = game.scene();
    const EntityId puff = addEmitter(scene);
    ParticleEmitter& emitter = scene.get<ParticleEmitter>(puff);
    emitter.emitting         = false;
    emitter.burst            = 10;
    emitter.lifetime         = {0.5f, 0.5f};
    emitter.destroyWhenSpent = true;
    const EntityId spark = scene.createEntity();
    scene.add(spark, Transform{});
    HierarchyOperations::setParent(scene, spark, puff);
    const EntityId waiting = addEmitter(scene);
    scene.get<ParticleEmitter>(waiting).emitting         = false;
    scene.get<ParticleEmitter>(waiting).destroyWhenSpent = true;

    bool aliveAt20 = false;
    game.run(60, [&](int frame) {
        if (frame == 20) aliveAt20 = scene.isAlive(puff);
    });

    CHECK(aliveAt20);
    CHECK(!scene.isAlive(puff));
    CHECK(!scene.isAlive(spark));
    CHECK(scene.isAlive(waiting));
}
