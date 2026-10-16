#include "physics/physics_support.h"

#include <glm/gtc/constants.hpp>

#include "core/math/curve.h"
#include "core/system.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/particle_emitter.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/particle/particle_system.h"
#include "system/render/render_view.h"
#include "system/visibility/visibility.h"

namespace {

// The real ParticleSystem over a real Scene, after the HierarchySystem it runs
// behind. Driving the system rather than restating its arithmetic is the point:
// a test that re-derives the rule passes whatever the rule becomes.
struct ParticleWorld {
    Scene           scene;
    TestFrame       frame{scene};
    Clock&          clock = frame.clock;
    HierarchySystem hierarchy;
    ParticleSystem  system;
    EntityId        emitter;

    ParticleWorld(float rate, uint32_t cap, float lifetime, int tickRate = 64) {
        // Paused with steps requested is how a sim delta is made to order: the
        // step count is exact at any tick rate, which a span of seconds is not.
        clock.setTickRate(tickRate);
        clock.setPaused(true);
        clock.beginFrame();
        emitter = addEmitter(rate, cap, lifetime);
    }

    // A still emitter whose particles stay where they are born.
    EntityId addEmitter(float rate, uint32_t cap, float lifetime, const glm::vec3& at = glm::vec3(0.0f)) {
        const EntityId id = scene.createEntity();
        Transform transform;
        transform.position = at;
        scene.add(id, transform);
        ParticleEmitter e;
        e.rate         = rate;
        e.maxParticles = cap;
        e.lifetime     = {lifetime, lifetime};
        e.speed        = {0.0f, 0.0f};
        e.acceleration = glm::vec3(0.0f);
        scene.add(id, e);
        return id;
    }

    ParticleEmitter& settings(EntityId id) { return scene.get<ParticleEmitter>(id); }
    ParticleEmitter& settings() { return settings(emitter); }

    // What the system published; before the first tick, an empty pool.
    const LiveParticles::Pool& pool(EntityId id) const {
        static const LiveParticles::Pool NONE;
        if (!frame.ctx.particles) return NONE;
        const auto found = frame.ctx.particles->pools.find(id.slot());
        return found != frame.ctx.particles->pools.end() ? found->second : NONE;
    }
    const LiveParticles::Pool& emitterState() const { return pool(emitter); }
    const std::vector<Particle>& particles(EntityId id) const { return pool(id).particles; }
    const std::vector<Particle>& particles() const { return particles(emitter); }

    void tick(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) {
            clock.requestStep(1);
            clock.beginFrame();
            // Drained, or the request stays pending and the next beginFrame
            // reports two steps' worth - the sim delta would grow every tick.
            while (clock.consumeFixedStep()) {}
            hierarchy.update(frame.ctx);
            system.update(frame.ctx);
        }
    }

    // A frame with nothing stepped, as a paused editor runs.
    void stand() {
        clock.beginFrame();
        hierarchy.update(frame.ctx);
        system.update(frame.ctx);
    }

    // The frame as the renderer is handed it, through a host's free view.
    RenderView view() const {
        Visibility seen;
        seen.hasCamera = true;
        RenderView out;
        out.build(scene, seen, nullptr, nullptr, nullptr, frame.ctx.particles, nullptr);
        return out;
    }
};

bool closeTo(const glm::vec3& a, const glm::vec3& b, float within = 1e-4f) {
    return glm::length(a - b) <= within;
}

// The rule lives in particle_system.cpp: at capacity only the fraction of a
// spawn survives, so an emitter held full for a long time cannot discharge
// every credit it accrued the instant a particle dies.
void testAnEmitterHeldAtCapacityBanksNothing() {
    std::printf("An emitter saturated for a long time, then let go:\n");

    ParticleWorld w(200.0f, 8, 1000.0f);   // long-lived, so nothing dies on its own
    w.tick(600);                            // about nine seconds of credit at 64 Hz

    check("it fills to its cap", w.particles().size() == 8);

    // The credit it could not spend must not be waiting. Banked whole, this
    // would be well over a thousand spawns owed the moment a slot frees.
    check("no whole spawn is banked while full", w.emitterState().spawnAccumulator < 1.0f);

    // Room for every particle owed, then one tick: what arrives is one tick's
    // worth, not nine seconds' worth. 200 a second at 64 Hz is three and an
    // eighth, so three or four, counted rather than bounded by the cap.
    w.settings().maxParticles = 10000;
    w.tick(1);
    const size_t arrived = w.particles().size() - 8;
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
    check("one tick alone spawns nothing", w.particles().empty());

    w.tick(191);   // 192 ticks total = three seconds
    const size_t spawned = w.particles().size();
    check("but the remainder carries, so it does emit", spawned > 0);
    std::printf("      slow emitter spawned %zu over 192 ticks\n", spawned);
    check("  at about its stated rate", spawned >= 60 && spawned <= 64);
}

void testParticlesAgeOutAndTheEmitterCanBeStopped() {
    std::printf("What ages out, and what stops:\n");

    ParticleWorld w(64.0f, 100, 0.25f);    // one a tick, a quarter second each
    w.tick(64);                             // a second: births and deaths in flight

    const size_t live = w.particles().size();
    std::printf("      live after 64 ticks: %zu\n", live);
    check("a short lifetime holds the count down", live > 0 && live <= 20);

    w.settings().emitting = false;
    w.tick(32);
    check("stopping stops the births", w.particles().size() < live);

    w.tick(64);
    check("  and the rest age out", w.particles().empty());
}

// The draws come from a generator the system owns, so a world loaded again
// draws what it drew the first time - rather than continuing a sequence the
// world before it had consumed.
void testAWorldLoadedAgainDrawsTheSame() {
    std::printf("The draws, in a world and in the same world loaded again:\n");

    ParticleWorld w(64.0f, 100, 1000.0f);
    const auto drawn = [&w]() {
        ParticleEmitter& emitter = w.settings();
        emitter.angle    = glm::radians(60.0f);
        emitter.speed    = {1.0f, 3.0f};
        emitter.lifetime = {500.0f, 1000.0f};
        emitter.size     = {0.1f, 0.4f};
        w.tick(4);
        std::vector<Particle> born = w.particles();
        return born;
    };
    const std::vector<Particle> first = drawn();

    // The same world again: cleared, which is what a load does, and rebuilt.
    w.scene.clear();
    w.emitter = w.addEmitter(64.0f, 100, 1000.0f);
    const std::vector<Particle> second = drawn();

    check("it spawns in both", !first.empty() && first.size() == second.size());
    bool same = !first.empty() && first.size() == second.size();
    for (size_t i = 0; same && i < first.size(); ++i) {
        same = first[i].velocity == second[i].velocity && first[i].lifetime == second[i].lifetime
            && first[i].size == second[i].size;
    }
    check("  and draws the same the second time", same);
}

// A puff is a burst from a box, by an emitter that goes once it is spent: the
// three things a game otherwise fakes with a rate, a timer and a moving emitter.
void testABurstFiresOnceFromItsBoxAndGoesWhenSpent() {
    std::printf("A burst, from a box, by an emitter that goes when spent:\n");

    ParticleWorld w(0.0f, 100, 0.25f);
    ParticleEmitter& emitter = w.settings();
    emitter.emitting         = false;
    emitter.burst            = 40;
    emitter.shape            = EmitShape::Box;
    emitter.halfExtents      = {1.0f, 0.0f, 2.0f};
    emitter.destroyWhenSpent = true;
    w.tick();

    const std::vector<Particle>& born = w.particles();
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
    check("  and is spent", w.settings().burst == 0);

    w.tick(8);
    check("it fires once, however long it stands", w.particles().size() <= 40);
    w.tick(16);
    check("its entity goes once its last particle has", !w.scene.isAlive(w.emitter));

    // One set up to wait for gameplay's burst waits for it.
    ParticleWorld idle(0.0f, 100, 0.25f);
    ParticleEmitter& waiting = idle.settings();
    waiting.emitting         = false;
    waiting.destroyWhenSpent = true;
    idle.tick(8);
    const bool waited = idle.scene.isAlive(idle.emitter);
    check("one not yet fired is not spent", waited);
    if (!waited) return;
    idle.settings().burst = 5;
    idle.tick(24);
    check("  and goes once its burst has", !idle.scene.isAlive(idle.emitter));

    // A burst the cap lets none of through has still fired.
    ParticleWorld full(0.0f, 0, 0.25f);
    full.settings().emitting         = false;
    full.settings().burst            = 5;
    full.settings().destroyWhenSpent = true;
    full.tick(2);
    check("one whose burst found no room goes too", !full.scene.isAlive(full.emitter));
}

// Keys are read straight between, the ends held past them, and none is the identity.
void testCurvesAndGradientsReadBetweenTheirKeys() {
    std::printf("Curves, gradients and ranges:\n");

    Math::Curve curve;
    check("a curve of no keys reads one", curve.at(0.5f) == 1.0f);
    curve.keys = {{0.2f, 2.0f}};
    check("  of one key, its value throughout", curve.at(0.0f) == 2.0f && curve.at(0.9f) == 2.0f);
    curve.keys = {{0.0f, 1.0f}, {0.5f, 3.0f}, {1.0f, 0.0f}};
    check("  of three, straight between them", curve.at(0.25f) == 2.0f && curve.at(0.75f) == 1.5f);
    check("  and its ends held past them", curve.at(-1.0f) == 1.0f && curve.at(2.0f) == 0.0f);
    curve.keys = {{1.0f, 0.0f}, {0.0f, 1.0f}, {0.5f, 3.0f}};
    check("  in any order", curve.at(0.25f) == 2.0f && curve.at(0.75f) == 1.5f && curve.at(-1.0f) == 1.0f);
    curve.keys = {{0.5f, 1.0f}, {0.5f, 2.0f}};
    check(
        "  two keys at one time step there, the last listed after",
        curve.at(0.4f) == 1.0f && curve.at(0.5f) == 2.0f && curve.at(0.6f) == 2.0f
    );

    Math::Gradient gradient;
    check("a gradient of no keys reads white", gradient.at(0.3f) == glm::vec4(1.0f));
    gradient.keys = {{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}}, {1.0f, {0.0f, 0.0f, 1.0f, 0.0f}}};
    check("  of two, straight between them", gradient.at(0.5f) == glm::vec4(0.5f, 0.0f, 0.5f, 0.5f));

    Math::Rng drawn(7);
    Math::Rng untouched(7);
    const Math::FloatRange constant{2.0f, 2.0f};
    check("a range of equal ends is that value", constant.draw(drawn) == 2.0f);
    check("  and draws nothing from the stream", drawn.nextU32() == untouched.nextU32());
    const Math::FloatRange span{1.0f, 3.0f};
    bool inside = true;
    for (int i = 0; i < 64; ++i) {
        const float v = span.draw(drawn);
        inside = inside && v >= 1.0f && v <= 3.0f;
    }
    check("a range draws between its ends", inside);
}

// Each shape bears where it says, sends the way it says, and turns with its emitter.
void testEachShapeBearsAndSendsAsItSays() {
    std::printf("Where each shape bears its particles, and which way it sends them:\n");

    constexpr uint32_t BURST  = 200;
    constexpr float    RADIUS = 2.0f;
    const float        ANGLE  = glm::radians(30.0f);
    const glm::vec3    HALF(1.0f, 0.5f, 0.25f);

    const auto burst = [&](EmitShape shape, bool surface, const glm::quat& turn = glm::quat(1, 0, 0, 0)) {
        ParticleWorld w(0.0f, 300, 100.0f);
        w.scene.get<Transform>(w.emitter).rotation = turn;
        ParticleEmitter& e = w.settings();
        e.emitting    = false;
        e.burst       = BURST;
        e.shape       = shape;
        e.radius      = RADIUS;
        e.angle       = ANGLE;
        e.halfExtents = HALF;
        e.fromSurface = surface;
        e.speed       = {1.0f, 1.0f};
        w.tick();
        std::vector<Particle> born = w.particles();
        return born;
    };
    // Whether every one of a burst keeps the rule, at where it was born and its velocity;
    // and whether any one does, for a spread that must not be a point.
    const auto every = [&](const std::vector<Particle>& born, auto&& rule) {
        bool all = born.size() == BURST;
        for (const Particle& p : born) all = all && rule(p.position, p.velocity);
        return all;
    };
    const auto some = [](const std::vector<Particle>& born, auto&& rule) {
        bool any = false;
        for (const Particle& p : born) any = any || rule(p.position, p.velocity);
        return any;
    };
    const auto inside = [](const glm::vec3& at, const glm::vec3&) { return glm::length(at) < 1.9f; };
    using Rule = bool (*)(const glm::vec3&, const glm::vec3&);
    const Rule outward = [](const glm::vec3& at, const glm::vec3& v) {
        return closeTo(at, v * glm::length(at), 1e-3f);
    };
    const float cosAngle = std::cos(ANGLE);

    const auto point = [](const glm::vec3& at, const glm::vec3& v) {
        return at == glm::vec3(0.0f) && std::abs(glm::length(v) - 1.0f) < 1e-5f;
    };
    const auto ball = [&](const glm::vec3& at, const glm::vec3& v) {
        return glm::length(at) <= RADIUS + 1e-4f && outward(at, v);
    };
    const auto shell = [&](const glm::vec3& at, const glm::vec3&) {
        return std::abs(glm::length(at) - RADIUS) < 1e-4f;
    };
    const auto upper = [&](const glm::vec3& at, const glm::vec3& v) { return v.y >= 0.0f && outward(at, v); };
    const auto cone = [&](const glm::vec3& at, const glm::vec3& v) {
        return at.y == 0.0f && glm::length(at) <= RADIUS + 1e-4f && v.y >= cosAngle - 1e-5f;
    };
    const auto box = [&](const glm::vec3& at, const glm::vec3& v) {
        return glm::all(glm::lessThanEqual(glm::abs(at), HALF)) && v.y >= cosAngle - 1e-5f;
    };
    const auto disc = [&](const glm::vec3& at, const glm::vec3& v) {
        return at.y == 0.0f && v.y == 0.0f && outward(at, v);
    };
    const auto onFace = [&](const glm::vec3& at, const glm::vec3&) {
        const glm::vec3 off = glm::abs(glm::abs(at) - HALF);
        return off.x < 1e-5f || off.y < 1e-5f || off.z < 1e-5f;
    };
    const auto leaning = [](const glm::vec3&, const glm::vec3& v) { return v.y < 0.99f; };

    check("a point bears at itself, every way at its speed", every(burst(EmitShape::Point, false), point));
    const std::vector<Particle> ballBorn = burst(EmitShape::Sphere, false);
    check("a sphere in its ball, outward", every(ballBorn, ball) && some(ballBorn, inside));
    check("  and on its surface when asked", every(burst(EmitShape::Sphere, true), shell));
    const std::vector<Particle> domeBorn = burst(EmitShape::Hemisphere, false);
    check("a hemisphere in its upper half", every(domeBorn, upper) && every(domeBorn, ball));
    const std::vector<Particle> coneBorn = burst(EmitShape::Cone, false);
    check("a cone on its disc, up within its angle", every(coneBorn, cone));
    check("  and spread over it", some(coneBorn, inside) && some(coneBorn, leaning));
    const std::vector<Particle> boxBorn = burst(EmitShape::Box, false);
    check("a box in its extents, up within its angle", every(boxBorn, box) && some(boxBorn, leaning));
    check("  and on its faces when asked", every(burst(EmitShape::Box, true), onFace));
    check(
        "a disc on its rim, outward in its plane",
        every(burst(EmitShape::Disc, true), disc) && every(burst(EmitShape::Disc, true), shell)
    );
    check("  and over it otherwise", some(burst(EmitShape::Disc, false), inside));

    // Evenly, not only within bounds: over a disc of radius 2 the mean squared distance out is 2,
    // through a ball of radius 2 the mean cubed is 4, and over a cap the mean height is halfway
    // between its rim and its top.
    const auto mean = [](const std::vector<Particle>& born, auto&& of) {
        double sum = 0.0;
        for (const Particle& p : born) sum += of(p);
        return born.empty() ? 0.0 : sum / static_cast<double>(born.size());
    };
    const auto squared = [](const Particle& p) { return glm::dot(p.position, p.position); };
    const auto cubed   = [](const Particle& p) { return std::pow(glm::length(p.position), 3.0f); };
    const auto height  = [](const Particle& p) { return p.velocity.y; };
    const double discSpread = mean(burst(EmitShape::Disc, false), squared);
    const double ballSpread = mean(ballBorn, cubed);
    const double capHeight  = mean(coneBorn, height);
    std::printf("      disc %.2f, ball %.2f, cap %.3f\n", discSpread, ballSpread, capHeight);
    const bool even = std::abs(discSpread - 2.0) < 0.25 && std::abs(ballSpread - 4.0) < 0.5;
    check("a disc and a ball spread evenly", even);
    check("  and a cone over its cap", std::abs(capHeight - 0.5 * (1.0 + cosAngle)) < 0.01);

    // A quarter turn about Z lays +Y along -X: the cone's axis turns with its emitter, and its
    // disc from XZ into YZ.
    const glm::quat quarter = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
    const auto along = [&](const glm::vec3& at, const glm::vec3& v) {
        return v.x <= -cosAngle + 1e-4f && std::abs(at.x) < 1e-4f;
    };
    const auto upright = [](const glm::vec3& at, const glm::vec3&) { return std::abs(at.y) > 0.5f; };
    const std::vector<Particle> turnedBorn = burst(EmitShape::Cone, false, quarter);
    check("a turned emitter turns its shape", every(turnedBorn, along) && some(turnedBorn, upright));
}

// Drag keeps exp(-drag) of a particle's velocity a second; turbulence stirs it.
void testDragAndTurbulenceMoveAParticle() {
    std::printf("Drag and turbulence:\n");

    ParticleWorld w(0.0f, 10, 100.0f);
    ParticleEmitter& dragged = w.settings();
    dragged.emitting = false;
    dragged.burst    = 1;
    dragged.shape    = EmitShape::Point;
    dragged.speed    = {4.0f, 4.0f};
    dragged.drag     = 1.0f;
    w.tick();
    w.tick(64);
    const float speed = glm::length(w.particles().front().velocity);
    std::printf("      speed after a second: %.4f\n", speed);
    check("drag keeps exp(-drag) of the speed a second", std::abs(speed - 4.0f * std::exp(-1.0f)) < 1e-3f);

    // The swirl neither bunches nor thins: its divergence is nothing anywhere.
    bool free = true;
    const float h = 1e-2f;
    const glm::vec3 probes[] = {{0.3f, -1.2f, 2.0f}, {5.0f, 0.1f, -3.3f}, glm::vec3(-7.0f)};
    for (const glm::vec3& at : probes) {
        float divergence = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            glm::vec3 step(0.0f);
            step[axis] = h;
            const glm::vec3 ahead  = ParticleSystem::swirlAt(at + step, 0.7f);
            const glm::vec3 behind = ParticleSystem::swirlAt(at - step, 0.7f);
            divergence += (ahead[axis] - behind[axis]) / (2.0f * h);
        }
        free = free && std::abs(divergence) < 1e-3f;
    }
    check("the swirl has no divergence", free);

    // Nor does it pass SWIRL_REACH on any axis, which a colliding step's gather is grown by - and
    // it comes close, so the bound is the swirl's own.
    Math::Rng rng(5);
    float most = 0.0f;
    for (int i = 0; i < 4096; ++i) {
        glm::vec3 at;
        for (int axis = 0; axis < 3; ++axis) at[axis] = rng.nextFloat(-20.0f, 20.0f);
        const glm::vec3 swirl = glm::abs(ParticleSystem::swirlAt(at, rng.nextFloat(0.0f, 100.0f)));
        most = std::max(most, std::max(swirl.x, std::max(swirl.y, swirl.z)));
    }
    std::printf("      the swirl reaches %.3f on an axis\n", most);
    const float reach = ParticleSystem::SWIRL_REACH;
    check("  and stays within SWIRL_REACH", most <= reach && most > 0.9f * reach);

    ParticleWorld stirred(0.0f, 10, 100.0f);
    ParticleEmitter& e = stirred.settings();
    e.emitting   = false;
    e.burst      = 1;
    e.turbulence = 5.0f;
    stirred.tick();
    stirred.tick(32);
    const float stirredSpeed = glm::length(stirred.particles().front().velocity);
    check("turbulence moves a particle born still", stirredSpeed > 0.1f);
}

// A moving emitter spawns by the metre along where it went, and lends its velocity.
void testAMovingEmitterSpawnsByDistanceAndLendsItsVelocity() {
    std::printf("An emitter on the move:\n");

    // Two side by side, one lending its velocity, which keeps its particles at its pace.
    ParticleWorld w(0.0f, 1000, 100.0f);
    const EntityId lender = w.addEmitter(0.0f, 1000, 100.0f, {0.0f, 0.0f, 1.0f});
    w.settings().ratePerMetre          = 8.0f;
    w.settings(lender).ratePerMetre    = 8.0f;
    w.settings(lender).inheritVelocity = 1.0f;
    w.tick();
    // A quarter metre a tick at 64 Hz is 16 m/s, and two particles a tick.
    const auto moveBoth = [&](float x) {
        w.scene.get<Transform>(w.emitter).position.x = x;
        w.scene.get<Transform>(lender).position.x    = x;
    };
    for (int i = 1; i <= 40; ++i) {
        moveBoth(0.25f * static_cast<float>(i));
        w.tick();
    }
    const std::vector<Particle>& born = w.particles();
    std::printf("      %zu particle(s) over ten metres\n", born.size());
    check("eight a metre over ten metres", born.size() == 80);
    bool along = born.size() == 80;
    bool aged  = born.size() == 80;
    for (size_t i = 0; along && i < born.size(); ++i) {
        along = std::abs(born[i].position.x - 0.125f * static_cast<float>(i + 1)) < 1e-4f;
        // The first of a tick's two fell due halfway through it: half a tick older.
        if (i % 2 == 0) aged = aged && std::abs(born[i].age - born[i + 1].age - 1.0f / 128.0f) < 1e-5f;
    }
    check("  each where the emitter was when it fell due", along);
    check("  and as old as the time since", aged);
    bool lent = !w.particles(lender).empty();
    for (const Particle& p : w.particles(lender)) {
        lent = lent && closeTo(p.velocity, {16.0f, 0.0f, 0.0f}, 1e-2f);
    }
    check("  each with the emitter's velocity, when it lends it", lent);

    // Dragged while nothing steps - a paused editor - it has not travelled when the world runs on.
    moveBoth(50.0f);
    w.stand();
    w.tick();
    check("a move made while paused is no travel", w.particles().size() == 80);

    ParticleWorld still(0.0f, 1000, 100.0f);
    still.settings().ratePerMetre = 8.0f;
    still.tick(40);
    check("a still one spawns nothing by distance", still.particles().empty());
}

// A colliding particle stops at the surface and turns back as much as bounce keeps; a strike
// counts only while it comes in faster than settling would bring it.
void testParticlesStrikeTheWorldAndBounce() {
    std::printf("Particles striking the world:\n");

    ParticleWorld w(0.0f, 10, 100.0f);
    addBox(w.scene, {0.0f, -0.5f, 0.0f}, {5.0f, 0.5f, 5.0f});
    const auto drop = [](ParticleWorld& world, EntityId id, float lifeLost) {
        world.scene.get<Transform>(id).position.y = 1.0f;
        ParticleEmitter& e = world.settings(id);
        e.emitting      = false;
        e.burst         = 1;
        e.acceleration  = {0.0f, -10.0f, 0.0f};
        e.collide       = true;
        e.bounce        = 0.5f;
        e.lifeLostOnHit = lifeLost;
    };
    drop(w, w.emitter, 0.0f);
    const EntityId fragile = w.addEmitter(0.0f, 10, 100.0f, {2.0f, 0.0f, 0.0f});
    drop(w, fragile, 1.0f);

    // A child that bears three where each of the parent's particles strikes; its own rate is unused.
    const EntityId sparks = w.addEmitter(50.0f, 100, 100.0f);
    HierarchyOperations::setParent(w.scene, sparks, w.emitter);
    w.scene.add(sparks, ParticleTrigger{ParticleEvent::Collision, 3});

    float lowest   = 1.0f;
    float falling  = 0.0f;   // its speed the step before the first bounce
    float kept     = -1.0f;  // the share of it the bounce kept
    bool  waiting  = false;
    for (int i = 0; i < 128; ++i) {
        const float before = w.particles().empty() ? 0.0f : w.particles().front().velocity.y;
        w.tick();
        if (w.particles().empty()) break;
        const Particle& p = w.particles().front();
        lowest = std::min(lowest, p.position.y);
        if (kept < 0.0f && p.velocity.y > 0.0f) {
            // Gravity's step is taken before the strike turns it.
            falling = -(before - 10.0f / 64.0f);
            kept    = p.velocity.y / falling;
        }
        if (i == 10) waiting = w.particles(sparks).empty();
    }
    std::printf("      lowest %.4f; the bounce kept %.3f of %.2f m/s\n", lowest, kept, falling);
    check("a particle stops at the surface", lowest >= 0.0f && lowest < 0.05f);
    check("  and turns back up with the share bounce keeps", std::abs(kept - 0.5f) < 1e-3f);
    check("one whose strike takes its whole life ends there", w.particles(fragile).empty());

    const std::vector<Particle>& born = w.particles(sparks);
    bool atFloor = !born.empty();
    for (const Particle& p : born) atFloor = atFloor && p.position.y >= 0.0f && p.position.y < 0.05f;
    check("a sub-emitter bears nothing before a strike", waiting);
    check("  then three for each strike, where it struck", born.size() % 3 == 0 && atFloor);

    // At rest on the floor it strikes it every step, at any step rate and however much it keeps of
    // a bounce; none of those counts.
    for (const auto& [rate, bounce] : {std::pair{64, 0.5f}, std::pair{144, 0.5f}, std::pair{144, 0.95f}}) {
        ParticleWorld rest(0.0f, 10, 1000.0f, rate);
        addBox(rest.scene, {0.0f, -0.5f, 0.0f}, {5.0f, 0.5f, 5.0f});
        drop(rest, rest.emitter, 0.0f);
        rest.settings(rest.emitter).acceleration = {0.0f, -9.81f, 0.0f};
        rest.settings(rest.emitter).bounce       = bounce;
        const EntityId puffs = rest.addEmitter(0.0f, 1000, 1000.0f);
        HierarchyOperations::setParent(rest.scene, puffs, rest.emitter);
        rest.scene.add(puffs, ParticleTrigger{ParticleEvent::Collision, 1});
        // A lively bounce takes longer to come to rest: each keeps nearly all of the one before.
        rest.tick(rate * (bounce < 0.9f ? 4 : 40));
        const size_t settled = rest.particles(puffs).size();
        rest.tick(rate * 4);
        std::printf("      at %d Hz, bounce %.2f: %zu strike(s) counted\n", rate, bounce, settled);
        const char* rule = rate == 64 ? "one settled counts no strike more"
            : bounce < 0.9f           ? "  nor at a higher step rate"
                                      : "  nor when it keeps nearly all of each bounce";
        // The few strikes of its first fall settle it; a lively bounce takes more of them.
        const size_t most = bounce < 0.9f ? 12 : 80;
        check(rule, settled > 0 && settled < most && rest.particles(puffs).size() == settled);
    }

    // Born on the floor and sent up off it, they leave it: a ray from the surface is no strike.
    ParticleWorld floor(0.0f, 50, 100.0f);
    addBox(floor.scene, {0.0f, -0.5f, 0.0f}, {5.0f, 0.5f, 5.0f});
    ParticleEmitter& launched = floor.settings();
    launched.emitting     = false;
    launched.burst        = 20;
    launched.collide      = true;
    launched.speed        = {2.0f, 2.0f};
    launched.acceleration = glm::vec3(0.0f);
    floor.tick(8);
    bool rose = floor.particles().size() == 20;
    for (const Particle& p : floor.particles()) rose = rose && p.position.y > 0.1f;
    check("ones born on a body leave it", rose);
}

// A sub-emitter bears where its parent's particles are born and die, and its own may bear more.
void testASubEmitterBearsWhereItsParentsParticlesAreBornAndDie() {
    std::printf("Sub-emitters on birth and death:\n");

    const glm::vec3 origin(3.0f, 1.0f, 0.0f);
    ParticleWorld w(0.0f, 10, 0.1f);
    w.scene.get<Transform>(w.emitter).position = origin;
    ParticleEmitter& parent = w.settings();
    parent.emitting = false;
    parent.burst    = 5;
    parent.shape    = EmitShape::Point;
    parent.speed    = {1.0f, 1.0f};

    const auto child = [&](EntityId of, ParticleEvent event, uint32_t count, float inherit, float life) {
        const EntityId id = w.addEmitter(30.0f, 100, life);
        w.settings(id).inheritVelocity = inherit;
        HierarchyOperations::setParent(w.scene, id, of);
        w.scene.add(id, ParticleTrigger{event, count});
        return id;
    };
    const EntityId onBirth = child(w.emitter, ParticleEvent::Birth, 1, 0.0f, 100.0f);
    const EntityId onDeath = child(w.emitter, ParticleEvent::Death, 2, 1.0f, 100.0f);
    // Born of the births the death child's own spawns queue, after the walk.
    const EntityId grandchild = child(onDeath, ParticleEvent::Birth, 1, 0.0f, 100.0f);

    w.tick(20);
    check("one for each birth, and none by its own rate", w.particles(onBirth).size() == 5);
    check("two for each death", w.particles(onDeath).size() == 10);
    check("  and one for each of those births in turn", w.particles(grandchild).size() == 10);
    // The parent's particles left the origin at 1 m/s and died a tenth of a second out; the death
    // child's were born there with their velocity, and went on the same way.
    bool there = !w.particles(onDeath).empty();
    for (const Particle& p : w.particles(onDeath)) {
        const glm::vec3 out = p.position - origin;
        there = there && glm::length(out) > 0.1f && std::abs(glm::length(p.velocity) - 1.0f) < 1e-4f
            && closeTo(glm::normalize(out), p.velocity, 1e-3f);
    }
    check("  where the parent's particles died, with their velocity", there);

    // A one-off puff waits for what its particles' deaths bore before it goes, for they go with it.
    ParticleWorld puff(0.0f, 10, 0.1f);
    puff.settings().emitting         = false;
    puff.settings().burst            = 2;
    puff.settings().destroyWhenSpent = true;
    const EntityId embers = puff.addEmitter(0.0f, 100, 0.5f);
    HierarchyOperations::setParent(puff.scene, embers, puff.emitter);
    puff.scene.add(embers, ParticleTrigger{ParticleEvent::Death, 3});
    puff.tick(20);
    const bool waits = puff.scene.isAlive(puff.emitter) && puff.particles(embers).size() == 6;
    check("a puff spent stays while its sub-emitter's particles live", waits);
    puff.tick(40);
    check("  and goes with them after", !puff.scene.isAlive(puff.emitter));
}

// A trail keeps where its particle was, newest first; a ribbon runs through an emitter's particles.
void testTrailsAndRibbonsFollowTheirParticles() {
    std::printf("Trails and ribbons:\n");

    ParticleWorld w(0.0f, 10, 0.5f);
    ParticleEmitter& e = w.settings();
    e.emitting    = false;
    e.burst       = 1;
    e.shape       = EmitShape::Point;
    e.speed       = {2.0f, 2.0f};
    e.render      = ParticleRender::Trail;
    e.trailLength = 0.25f;
    w.tick(16);
    // A second one, born later and living longer, outlives the first: its trail must move with it.
    // Looked at three ticks after the first dies, before a trail left behind could be written over.
    e.lifetime = {100.0f, 100.0f};
    e.burst    = 1;
    w.tick(20);

    const LiveParticles::Pool& pool = w.emitterState();
    const bool alone = pool.particles.size() == 1;
    check("the first has died", alone);
    if (!alone) return;
    const Particle&  p      = pool.particles.front();
    const glm::vec3* points = pool.trailOf(0);
    check("a trail fills to its points", p.trailCount == LiveParticles::TRAIL_POINTS);
    bool behind = true;
    for (uint32_t k = 0; k < p.trailCount; ++k) {
        const glm::vec3 back = p.position - points[k];
        behind = behind && glm::dot(glm::normalize(back), glm::normalize(p.velocity)) > 0.999f;
        if (k > 0) behind = behind && glm::length(back) > glm::length(p.position - points[k - 1]);
    }
    check("  each further back along its own path, newest first", behind);

    const RenderView trail = w.view();
    const bool one = trail.stripsAdditive.size() == 1 && trail.particlesAdditive.empty();
    check("a trail draws as a strip, not a card", one);
    if (one) {
        const ParticleStrip& strip = trail.stripsAdditive.front();
        const StripPoint&    head  = trail.stripPoints[strip.first];
        const StripPoint&    tail  = trail.stripPoints[strip.first + strip.count - 1];
        check("  through the particle and every point", strip.count == p.trailCount + 1);
        check("  from its head", head.params.x == 0.0f && closeTo(glm::vec3(head.positionWidth), p.position));
        check(
            "  narrowing and fading to nothing",
            tail.params.x == 1.0f && tail.positionWidth.w == 0.0f && tail.color.a == 0.0f
        );
    }

    // Turned to cards, then back to trails while nothing steps: it draws no trail it does not keep.
    w.settings().render = ParticleRender::Billboard;
    w.tick();
    w.settings().render = ParticleRender::Trail;
    w.stand();
    check("one turned to trails while paused draws none yet", w.view().stripsAdditive.empty());

    ParticleWorld r(64.0f, 100, 100.0f);
    r.settings().shape  = EmitShape::Point;
    r.settings().speed  = {1.0f, 1.0f};
    r.settings().render = ParticleRender::Ribbon;
    r.settings().blend  = ParticleBlend::Alpha;
    r.tick(10);
    const RenderView ribbon = r.view();
    const std::vector<Particle>& live = r.particles();
    const bool single = ribbon.stripsAlpha.size() == 1;
    check("a ribbon is one strip, by its blend", single && ribbon.particlesAlpha.empty());
    if (single) {
        const ParticleStrip& strip = ribbon.stripsAlpha.front();
        const glm::vec3      head  = glm::vec3(ribbon.stripPoints[strip.first].positionWidth);
        check("  through every particle", strip.count == live.size());
        check("  the newest at its head", closeTo(head, live.back().position));
    }
}

// A sheet plays once over a life, or loops at its rate, from the frame a particle was born on.
void testASheetPlaysOnceOverALifeOrAtItsRate() {
    std::printf("Which frame of its sheet a particle shows:\n");

    ParticleEmitter e;
    e.sheetColumns = 2;
    e.sheetRows    = 2;
    const auto frameAt = [&](float age, uint32_t start) { return sheetFrameAt(e, age, 4.0f, start).frame; };
    const bool once = frameAt(0.0f, 0) == 0 && frameAt(1.5f, 0) == 1 && frameAt(3.9f, 0) == 3
        && frameAt(4.0f, 0) == 3;
    check("over a life, a quarter of it each, held on the last", once);
    check("  from the frame it was born on, round the sheet", frameAt(1.5f, 3) == 0);

    // Cross-faded, a life of four frames spans three fades and ends on the last frame whole.
    e.sheetBlend = true;
    const SheetFrame mid = sheetFrameAt(e, 2.0f, 4.0f, 0);
    const SheetFrame end = sheetFrameAt(e, 4.0f, 4.0f, 0);
    const bool halfway = mid.frame == 1 && mid.next == 2 && std::abs(mid.blend - 0.5f) < 1e-5f;
    check("cross-faded, halfway through a life is halfway from frame 1 into 2", halfway);
    check("  and its end is the last frame, faded into nothing", end.frame == 3 && end.blend == 0.0f);
    e.sheetBlend = false;

    e.sheetFps = 10.0f;
    check("at a rate, looping", frameAt(0.25f, 0) == 2 && frameAt(0.45f, 0) == 0);
    e.sheetBlend = true;
    const SheetFrame wrap = sheetFrameAt(e, 0.35f, 4.0f, 0);
    const bool round = wrap.frame == 3 && wrap.next == 0 && std::abs(wrap.blend - 0.5f) < 1e-4f;
    check("  cross-faded, the last frame into the first", round);
    e.sheetColumns = 0;
    check("a sheet of no columns is one column", sheetFrames(e) == 2);
}

// An emitter holding @p p, in the pool ParticleSystem would keep for it.
EntityId addEmitter(Scene& scene, LiveParticles& live, const ParticleEmitter& e, const Particle& p) {
    const EntityId id = scene.createEntity();
    scene.add(id, e);
    LiveParticles::Pool& pool = live.pools[id.slot()];
    pool.emitter = id;
    pool.particles.push_back(p);
    return id;
}

// What the renderer is handed for a particle: its frame's rect, the top row first, its angle, its
// size and colour at its age, its streak behind it, and its texture once however many show it.
void testAParticleIsHandedOnAsItsFrameTurnedOrStreaked() {
    std::printf("A particle as the frame hands it to the renderer:\n");

    Scene scene;
    ResourceManager resources;
    const TextureHandle sheet = resources.add(TextureAsset{}, "particle:sheet");
    LiveParticles live;
    ParticleEmitter cards;
    cards.blend        = ParticleBlend::Alpha;
    cards.emission     = 0.4f;
    cards.texture      = sheet;
    cards.sheetColumns = 4;
    cards.sheetRows    = 2;
    Particle turned;
    turned.lifetime = 8.0f;
    turned.age      = 5.5f;
    turned.angle    = 0.5f;
    turned.spin     = 2.0f;
    addEmitter(scene, live, cards, turned);
    addEmitter(scene, live, cards, turned);
    ParticleEmitter sparks;
    sparks.render       = ParticleRender::Stretched;
    sparks.stretch      = 0.1f;
    sparks.intensity    = 3.0f;
    sparks.color.keys   = {{0.0f, {1.0f, 0.5f, 0.25f, 0.8f}}};
    sparks.sizeOverLife = {{{0.0f, 1.0f}, {1.0f, 0.0f}}};
    Particle fast;
    fast.velocity = {4.0f, 0.0f, -2.0f};
    fast.size     = 0.5f;
    fast.lifetime = 4.0f;
    fast.age      = 1.0f;
    addEmitter(scene, live, sparks, fast);
    ParticleEmitter haze;
    haze.blend      = ParticleBlend::Distortion;
    haze.distortion = 0.05f;
    addEmitter(scene, live, haze, Particle{});

    Visibility seen;
    seen.hasCamera = true;
    RenderView view;
    view.build(scene, seen, nullptr, nullptr, nullptr, &live, nullptr);
    check("one texture, listed once", view.particleTextures.size() == 1 && view.particleTextures[0] == sheet);
    const bool split = view.particlesAlpha.size() == 2 && view.particlesAdditive.size() == 1
        && view.particlesDistortion.size() == 1;
    check("  split by blend", split);
    if (!split) return;
    const ParticleData& card  = view.particlesAlpha[0];
    const ParticleData& spark = view.particlesAdditive[0];
    check("a card names its texture", card.params.z == 1.0f && spark.params.z == 0.0f);
    // 5.5 of 8 seconds is frame 5: the second row's second, the sheet's top row first.
    check("  shows its frame", card.uvRect == glm::vec4(0.25f, 0.0f, 0.25f, 0.5f));
    check("  turned by where it began and how long it spun", card.params.y == 0.5f + 2.0f * 5.5f);
    check("  and carries what it gives off", card.trail.w == 0.4f);
    check("a spark streaks behind it by its stretch", glm::vec3(spark.trail) == glm::vec3(-0.4f, 0.0f, 0.2f));
    check("  and a card not at all", glm::vec3(card.trail) == glm::vec3(0.0f));
    check("  its size by its curve at its age", spark.positionSize.w == 0.375f);
    check("  its colour by its gradient, brightened", spark.color == glm::vec4(3.0f, 1.5f, 0.75f, 0.8f));
    check("a haze particle carries how far it bends", view.particlesDistortion[0].trail.w == 0.05f);
}

// An emitter's seenBy reads as a mesh's, through a scene camera alone; particles are drawn by the
// eye and nothing else, so one seen by all but the eye is drawn nowhere.
void testAnEmitterIsSeenByAsAMeshIs() {
    std::printf("Who sees an emitter's particles:\n");

    Scene scene;
    LiveParticles live;
    for (const SeenBy seenBy : {SeenBy::All, SeenBy::AllButEye, SeenBy::Viewmodel}) {
        ParticleEmitter emitter;
        emitter.seenBy = seenBy;
        addEmitter(scene, live, emitter, Particle{});
    }
    const auto viewmodels = [](const RenderView& view) {
        size_t count = 0;
        for (const ParticleData& p : view.particlesAdditive) count += p.params.w == 1.0f ? 1 : 0;
        return count;
    };

    Visibility eye;
    eye.hasCamera    = true;
    eye.cameraEntity = scene.createEntity();
    RenderView view;
    view.build(scene, eye, nullptr, nullptr, nullptr, &live, nullptr);
    check("a scene camera draws nothing of one seen by all but the eye", view.particlesAdditive.size() == 2);
    check("  and the viewmodel's take its projection", viewmodels(view) == 1);

    Visibility freeView;
    freeView.hasCamera = true;
    view.build(scene, freeView, nullptr, nullptr, nullptr, &live, nullptr);
    check("a host's free view draws them all", view.particlesAdditive.size() == 3);
    check("  where they are", viewmodels(view) == 0);
}

// Asked for, each particle is born on a frame and at an angle of its own.
void testParticlesAreBornOnFramesAndAnglesOfTheirOwn() {
    std::printf("Particles born on frames and at angles of their own:\n");

    ParticleWorld w(0.0f, 100, 10.0f);
    ParticleEmitter& emitter = w.settings();
    emitter.sheetColumns = 4;
    emitter.sheetRows    = 4;
    emitter.randomFrame  = true;
    emitter.rotation     = {0.0f, glm::two_pi<float>()};
    emitter.burst        = 64;
    w.tick();
    bool inSheet = true;
    bool frames  = false;
    bool angles  = false;
    const Particle& first = w.particles().front();
    for (const Particle& p : w.particles()) {
        inSheet = inSheet && p.frame < 16 && p.angle >= 0.0f && p.angle <= glm::two_pi<float>();
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
    testAWorldLoadedAgainDrawsTheSame();
    testCurvesAndGradientsReadBetweenTheirKeys();
    testEachShapeBearsAndSendsAsItSays();
    testDragAndTurbulenceMoveAParticle();
    testAMovingEmitterSpawnsByDistanceAndLendsItsVelocity();
    testParticlesStrikeTheWorldAndBounce();
    testASubEmitterBearsWhereItsParentsParticlesAreBornAndDie();
    testTrailsAndRibbonsFollowTheirParticles();
    testASheetPlaysOnceOverALifeOrAtItsRate();
    testAParticleIsHandedOnAsItsFrameTurnedOrStreaked();
    testAnEmitterIsSeenByAsAMeshIs();
    testParticlesAreBornOnFramesAndAnglesOfTheirOwn();
}
