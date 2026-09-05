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

    ParticleEmitter& emitterState() { return scene.get<ParticleEmitter>(emitter); }

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

// The roadmap names "the particle burst banking" as a defect a test pins
// forever. The rule lives in particle_system.cpp: at capacity only the fraction
// survives, so an emitter held full for a long time cannot discharge every
// credit it accrued the instant a particle dies.
void testAnEmitterHeldAtCapacityBanksNothing() {
    std::printf("An emitter saturated for a long time, then let go:\n");

    ParticleWorld w(200.0f, 8, 1000.0f);   // long-lived, so nothing dies on its own
    w.tick(600);                            // about nine seconds of credit at 64 Hz

    check("it fills to its cap", w.emitterState().particles.size() == 8);
    check("  and never past it", w.emitterState().particles.size() <= 8);

    // The credit it could not spend must not be waiting. Banked whole, this
    // would be well over a thousand spawns owed the moment a slot frees.
    check("no whole spawn is banked while full", w.emitterState().spawnAccumulator < 1.0f);

    // Free every slot and run one tick: what comes back is one tick's worth,
    // not nine seconds' worth.
    w.emitterState().particles.clear();
    w.tick(1);
    check("one tick after emptying spawns one tick's worth",
          w.emitterState().particles.size() <= 8);
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

    w.emitterState().emitting = false;
    w.tick(32);
    check("stopping stops the births", w.emitterState().particles.size() < live);

    w.tick(64);
    check("  and the rest age out", w.emitterState().particles.empty());
}

} // namespace

void runParticleTests() {
    testAnEmitterHeldAtCapacityBanksNothing();
    testASlowEmitterStillEmits();
    testParticlesAgeOutAndTheEmitterCanBeStopped();
}
