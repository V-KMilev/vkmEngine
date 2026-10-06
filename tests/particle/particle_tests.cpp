#include "support.h"

#include "core/system.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/particle_emitter.h"
#include "system/particle/particle_system.h"

namespace {

// The real ParticleSystem over a real Scene. Driving the system rather than
// restating its arithmetic is the point: a test that re-derives the rule passes
// whatever the rule becomes.
struct ParticleWorld {
    Scene           scene;
    TestFrame       frame{scene};
    Clock&          clock = frame.clock;
    ParticleSystem  system;
    EntityId        emitter;

    ParticleWorld(float rate, uint32_t cap, float lifetime) {
        // Paused with steps requested is how a sim delta is made to order: the
        // step count is exact at any tick rate, which a span of seconds is not.
        clock.setTickRate(64);
        clock.setPaused(true);
        clock.beginFrame();
        addEmitter(rate, cap, lifetime);
    }

    void addEmitter(float rate, uint32_t cap, float lifetime) {
        emitter = scene.createEntity();
        scene.add(emitter, Transform{});
        ParticleEmitter e;
        e.rate         = rate;
        e.maxParticles = cap;
        e.lifetime     = lifetime;
        e.spread       = 0.0f;                    // no rng in the answer
        e.velocity     = {0.0f, 0.0f, 0.0f};
        e.acceleration = {0.0f, 0.0f, 0.0f};
        scene.add(emitter, e);
    }

    // What the system published.
    const LiveParticles::Pool& emitterState() const { return frame.ctx.particles->pools.at(emitter.slot()); }

    void tick(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) {
            clock.requestStep(1);
            clock.beginFrame();
            // Drained, or the request stays pending and the next beginFrame
            // reports two steps' worth - the sim delta would grow every tick.
            while (clock.consumeFixedStep()) {}
            system.update(frame.ctx);
        }
    }
};

// The rule lives in particle_system.cpp: at capacity only the fraction of a
// spawn survives, so an emitter held full for a long time cannot discharge
// every credit it accrued the instant a particle dies.
void testAnEmitterHeldAtCapacityBanksNothing() {
    std::printf("An emitter saturated for a long time, then let go:\n");

    ParticleWorld w(200.0f, 8, 1000.0f);   // long-lived, so nothing dies on its own
    w.tick(600);                            // about nine seconds of credit at 64 Hz

    check("it fills to its cap", w.emitterState().particles.size() == 8);

    // The credit it could not spend must not be waiting. Banked whole, this
    // would be well over a thousand spawns owed the moment a slot frees.
    check("no whole spawn is banked while full", w.emitterState().spawnAccumulator < 1.0f);

    // Room for every particle owed, then one tick: what arrives is one tick's
    // worth, not nine seconds' worth. 200 a second at 64 Hz is three and an
    // eighth, so three or four, counted rather than bounded by the cap.
    w.scene.get<ParticleEmitter>(w.emitter).maxParticles = 10000;
    w.tick(1);
    const size_t arrived = w.emitterState().particles.size() - 8;
    std::printf("      %zu particle(s) one tick after the cap is lifted\n", arrived);
    check(
        "one tick after the cap is lifted spawns one tick's worth, not the backlog",
        arrived >= 3 && arrived <= 4
    );
}

void testASlowEmitterStillEmits() {
    std::printf("An emitter slower than one particle a tick:\n");

    // 21/s at 64 Hz is a third of a particle per tick. Truncating instead of
    // carrying the remainder is an emitter that never emits at all.
    ParticleWorld w(21.0f, 1000, 1000.0f);
    w.tick(1);
    check("one tick alone spawns nothing", w.emitterState().particles.empty());

    w.tick(191);   // 192 ticks total = three seconds
    const size_t spawned = w.emitterState().particles.size();
    check("but the remainder carries, so it does emit", spawned > 0);
    std::printf("      slow emitter spawned %zu over 192 ticks\n", spawned);
    check("  at about its stated rate", spawned >= 60 && spawned <= 64);
}

void testParticlesAgeOutAndTheEmitterCanBeStopped() {
    std::printf("What ages out, and what stops:\n");

    ParticleWorld w(64.0f, 100, 0.25f);    // one a tick, a quarter second each
    w.tick(64);                             // a second: births and deaths in flight

    const size_t live = w.emitterState().particles.size();
    std::printf("      live after 64 ticks: %zu\n", live);
    check("a short lifetime holds the count down", live > 0 && live <= 20);

    w.scene.get<ParticleEmitter>(w.emitter).emitting = false;
    w.tick(32);
    check("stopping stops the births", w.emitterState().particles.size() < live);

    w.tick(64);
    check("  and the rest age out", w.emitterState().particles.empty());
}

// The spread is drawn from a generator the system owns, so a world loaded
// again draws what it drew the first time - rather than continuing a sequence
// the world before it had consumed.
void testAWorldLoadedAgainDrawsTheSameSpread() {
    std::printf("The spread, in a world and in the same world loaded again:\n");

    ParticleWorld w(64.0f, 100, 1000.0f);
    const auto spread = [&w]() {
        ParticleEmitter& emitter = w.scene.get<ParticleEmitter>(w.emitter);
        emitter.spread = 1.0f;
        w.tick(4);
        std::vector<glm::vec3> velocities;
        for (const Particle& p : w.emitterState().particles) velocities.push_back(p.velocity);
        return velocities;
    };
    const std::vector<glm::vec3> first = spread();

    // The same world again: cleared, which is what a load does, and rebuilt.
    w.scene.clear();
    w.addEmitter(64.0f, 100, 1000.0f);
    const std::vector<glm::vec3> second = spread();

    check("it spawns in both", !first.empty() && first.size() == second.size());
    bool same = !first.empty() && first.size() == second.size();
    for (size_t i = 0; same && i < first.size(); ++i) same = first[i] == second[i];
    check("  and draws the same spread the second time", same);
}

} // namespace

void runParticleTests() {
    testAnEmitterHeldAtCapacityBanksNothing();
    testASlowEmitterStillEmits();
    testParticlesAgeOutAndTheEmitterCanBeStopped();
    testAWorldLoadedAgainDrawsTheSameSpread();
}
