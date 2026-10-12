#include "support.h"

#include "core/system.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/particle_emitter.h"
#include "system/particle/particle_system.h"
#include "system/render/render_view.h"
#include "system/visibility/visibility.h"

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

// A puff is a burst from a box, by an emitter that goes once it is spent: the
// three things a game otherwise fakes with a rate, a timer and a moving emitter.
void testABurstFiresOnceFromItsBoxAndGoesWhenSpent() {
    std::printf("A burst, from a box, by an emitter that goes when spent:\n");

    ParticleWorld w(0.0f, 100, 0.25f);
    ParticleEmitter& emitter = w.scene.get<ParticleEmitter>(w.emitter);
    emitter.emitting         = false;
    emitter.burst            = 40;
    emitter.halfExtents      = {1.0f, 0.0f, 2.0f};
    emitter.destroyWhenSpent = true;
    w.tick();

    const std::vector<Particle>& born = w.emitterState().particles;
    check("a burst spawns its whole count at once", born.size() == 40);
    bool inside = true;
    bool spread = false;
    for (const Particle& p : born) {
        const glm::vec3 at = p.position;
        inside = inside && std::abs(at.x) <= 1.0f && at.y == 0.0f && std::abs(at.z) <= 2.0f;
        spread = spread || std::abs(p.position.z) > 1.0f;
    }
    check("  inside its box", inside);
    check("  and over all of it, not at a point", spread);
    check("  and is spent", w.scene.get<ParticleEmitter>(w.emitter).burst == 0);

    w.tick(8);
    check("it fires once, however long it stands", w.emitterState().particles.size() <= 40);
    w.tick(16);
    check("its entity goes once its last particle has", !w.scene.isAlive(w.emitter));

    // One set up to wait for gameplay's burst waits for it.
    ParticleWorld idle(0.0f, 100, 0.25f);
    ParticleEmitter& waiting = idle.scene.get<ParticleEmitter>(idle.emitter);
    waiting.emitting         = false;
    waiting.destroyWhenSpent = true;
    idle.tick(8);
    const bool waited = idle.scene.isAlive(idle.emitter);
    check("one not yet fired is not spent", waited);
    if (!waited) return;
    idle.scene.get<ParticleEmitter>(idle.emitter).burst = 5;
    idle.tick(24);
    check("  and goes once its burst has", !idle.scene.isAlive(idle.emitter));
}

// A sheet plays once over a life, or loops at its rate, from the frame a particle was born on.
void testASheetPlaysOnceOverALifeOrAtItsRate() {
    std::printf("Which frame of its sheet a particle shows:\n");

    ParticleEmitter e;
    e.sheetColumns = 2;
    e.sheetRows    = 2;
    const bool once = sheetFrameAt(e, 0.0f, 4.0f, 0) == 0 && sheetFrameAt(e, 1.5f, 4.0f, 0) == 1
        && sheetFrameAt(e, 3.9f, 4.0f, 0) == 3 && sheetFrameAt(e, 4.0f, 4.0f, 0) == 3;
    check("over a life, a quarter of it each, held on the last", once);
    check("  from the frame it was born on, round the sheet", sheetFrameAt(e, 1.5f, 4.0f, 3) == 0);
    e.sheetFps = 10.0f;
    check("at a rate, looping", sheetFrameAt(e, 0.25f, 4.0f, 0) == 2 && sheetFrameAt(e, 0.45f, 4.0f, 0) == 0);
    e.sheetColumns = 0;
    check("a sheet of no columns is one column", sheetFrames(e) == 2);
}

// What the renderer is handed for a particle: its frame's rect, the top row first, its angle,
// its trail behind it, and its texture once however many show it.
void testAParticleIsHandedOnAsItsFrameTurnedOrStreaked() {
    std::printf("A particle as the frame hands it to the renderer:\n");

    Scene scene;
    ResourceManager resources;
    const TextureHandle sheet = resources.add(TextureAsset{}, "particle:sheet");
    LiveParticles live;
    const auto emitter = [&](const ParticleEmitter& e, const Particle& p) {
        const EntityId id = scene.createEntity();
        scene.add(id, e);
        LiveParticles::Pool& pool = live.pools[id.slot()];
        pool.emitter = id;
        pool.particles.push_back(p);
    };
    ParticleEmitter cards;
    cards.additive     = false;
    cards.texture      = sheet;
    cards.sheetColumns = 4;
    cards.sheetRows    = 2;
    cards.spin         = 2.0f;
    Particle turned;
    turned.lifetime = 8.0f;
    turned.age      = 5.5f;
    turned.angle    = 0.5f;
    emitter(cards, turned);
    emitter(cards, turned);
    ParticleEmitter sparks;
    sparks.stretch = 0.1f;
    Particle fast;
    fast.velocity = {4.0f, 0.0f, -2.0f};
    emitter(sparks, fast);

    Visibility seen;
    seen.hasCamera = true;
    RenderView view;
    view.build(scene, seen, nullptr, nullptr, nullptr, &live, nullptr);
    check("one texture, listed once", view.particleTextures.size() == 1 && view.particleTextures[0] == sheet);
    const bool alpha = view.particlesAlpha.size() == 2 && view.particlesAdditive.size() == 1;
    check("  split by blend", alpha);
    if (!alpha) return;
    const ParticleData& card  = view.particlesAlpha[0];
    const ParticleData& spark = view.particlesAdditive[0];
    check("a card names its texture", card.params.z == 1.0f && spark.params.z == 0.0f);
    // 5.5 of 8 seconds is frame 5: the second row's second, the sheet's top row first.
    check("  shows its frame", card.uvRect == glm::vec4(0.25f, 0.0f, 0.25f, 0.5f));
    check("  turned by where it began and how long it spun", card.params.y == 0.5f + 2.0f * 5.5f);
    check("a spark streaks behind it by its stretch", spark.trail == glm::vec4(-0.4f, 0.0f, 0.2f, 0.0f));
    check("  and a card not at all", card.trail == glm::vec4(0.0f));
}

// Asked for, each particle is born on a frame and at an angle of its own.
void testParticlesAreBornOnFramesAndAnglesOfTheirOwn() {
    std::printf("Particles born on frames and at angles of their own:\n");

    ParticleWorld w(0.0f, 100, 10.0f);
    ParticleEmitter& emitter = w.scene.get<ParticleEmitter>(w.emitter);
    emitter.sheetColumns   = 4;
    emitter.sheetRows      = 4;
    emitter.randomFrame    = true;
    emitter.randomRotation = true;
    emitter.burst          = 64;
    w.tick();
    bool inSheet = true;
    bool frames  = false;
    bool angles  = false;
    const Particle& first = w.emitterState().particles.front();
    for (const Particle& p : w.emitterState().particles) {
        inSheet = inSheet && p.frame < 16 && p.angle >= 0.0f && p.angle < glm::two_pi<float>();
        frames  = frames || p.frame != first.frame;
        angles  = angles || p.angle != first.angle;
    }
    check("on frames of the sheet, at angles of a turn", inSheet);
    check("  and not all the same", frames && angles);
}

} // namespace

void runParticleTests() {
    testABurstFiresOnceFromItsBoxAndGoesWhenSpent();
    testAnEmitterHeldAtCapacityBanksNothing();
    testASlowEmitterStillEmits();
    testParticlesAgeOutAndTheEmitterCanBeStopped();
    testAWorldLoadedAgainDrawsTheSameSpread();
    testASheetPlaysOnceOverALifeOrAtItsRate();
    testAParticleIsHandedOnAsItsFrameTurnedOrStreaked();
    testParticlesAreBornOnFramesAndAnglesOfTheirOwn();
}
