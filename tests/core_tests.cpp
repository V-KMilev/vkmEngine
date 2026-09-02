#include "support.h"

namespace {

// The convention this engine turns on, pinned so that changing it is a decision
// rather than an accident. Right-handed, +Y up, forward -Z - glm's own, and the
// view space the renderer works in, which is what makes screen-right the plain
// +X and the obvious line of code the correct one.
//
// It was +Z forward once, which put right at -X and shipped three
// mirrored controls. These three assertions are what made changing it a
// deliberate act: they failed, and were changed on purpose. The rest below hold
// under either convention and are what would have caught a half-finished flip.
void testMathConvention() {
    std::printf("Math - the axis convention:\n");

    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 forward = Math::computeForward(identity);
    const glm::vec3 right   = Math::computeRight(identity);
    const glm::vec3 up      = Math::computeUp(identity);

    check("forward is -Z", sameDirection(forward, {0.0f, 0.0f, -1.0f}));
    check("up is +Y",      sameDirection(up,      {0.0f, 1.0f,  0.0f}));
    check("right is +X",   sameDirection(right,   {1.0f, 0.0f,  0.0f}));

    // Convention-independent: whatever forward means, the three have to agree.
    // A half-finished change of convention shows up here rather than as a
    // character walking sideways.
    check("the basis is right-handed: right x up == -forward",
          sameDirection(glm::cross(right, up), -forward));
    check("  and orthogonal", std::fabs(glm::dot(right, up)) < 1e-3f
                           && std::fabs(glm::dot(right, forward)) < 1e-3f
                           && std::fabs(glm::dot(up, forward)) < 1e-3f);

    // Also convention-independent, and the one that would have caught the bug
    // it was written for: glm::quatLookAt builds for -Z, so calling it directly
    // aims everything exactly backwards.
    const glm::vec3 targets[] = {
        { 1.0f,  0.0f,  0.0f}, {-1.0f,  0.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f}, { 0.0f,  0.0f, -1.0f},
        { 1.0f,  0.0f,  1.0f}, {-0.4f,  0.3f,  0.9f}
    };
    bool roundTrips = true;
    for (const glm::vec3& target : targets) {
        const glm::vec3 aimed = Math::computeForward(Math::lookRotation(target));
        if (!sameDirection(aimed, glm::normalize(target))) roundTrips = false;
    }
    check("lookRotation faces what it is given", roundTrips);

    // A quarter turn about up takes forward onto right, in that direction and
    // no other. Gets the sign of a rotation wrong and this is what says so.
    const glm::quat quarter = glm::angleAxis(glm::half_pi<float>(), up);
    check("a quarter turn about up sends forward to -right",
          sameDirection(Math::computeForward(quarter), -right));

    // The pair the flip broke, and broke silently: a look control maps
    // angles to a rotation and reverses it to re-derive them, and the flip
    // moved one side. The camera flew correctly and jumped the moment anything
    // took hold of one it had not moved - which passes a quick try, because
    // the first thing anyone does still works.
    bool anglesRoundTrip = true;
    bool pitchRaises = true;
    for (float yawDeg : {0.0f, 37.0f, -120.0f, 179.0f}) {
        for (float pitchDeg : {0.0f, 42.0f, -60.0f}) {
            const float yaw = glm::radians(yawDeg);
            const float pitch = glm::radians(pitchDeg);

            const glm::quat aim = Math::fromYawPitch(yaw, pitch);
            const glm::vec3 aimed = Math::computeForward(aim);
            float backYaw = 0.0f;
            float backPitch = 0.0f;
            Math::toYawPitch(aimed, backYaw, backPitch);

            const glm::vec3 again =
                Math::computeForward(Math::fromYawPitch(backYaw, backPitch));
            if (!sameDirection(aimed, again)) anglesRoundTrip = false;
            // A rising pitch has to raise the view, whatever the convention.
            if (pitchDeg > 0.0f && aimed.y <= 0.0f) pitchRaises = false;
            if (pitchDeg < 0.0f && aimed.y >= 0.0f) pitchRaises = false;
        }
    }
    check("angles round-trip through a direction and back", anglesRoundTrip);
    check("  and a rising pitch raises the view", pitchRaises);

    // Yaw zero has to face forward, or every authored angle means something
    // else than it did.
    float zeroYaw = 0.0f;
    float zeroPitch = 0.0f;
    Math::toYawPitch(forward, zeroYaw, zeroPitch);
    check("  with yaw and pitch zero looking straight ahead",
          std::fabs(zeroYaw) < 1e-3f && std::fabs(zeroPitch) < 1e-3f);

    check("worldRotationOf recovers a rotation from its matrix",
          sameDirection(Math::computeForward(
              Math::worldRotationOf(glm::mat4_cast(quarter))),
              Math::computeForward(quarter)));
}

// The cadence a project asks for is the cadence fixedUpdate runs at, and for a
// networked game the rate the wire is clocked by.
void testTickRate() {
    std::printf("Tick rate:\n");

    Clock clock;
    check("a fresh clock ticks at the engine default",
          nearly(clock.getFixedStep(), 1.0f / static_cast<float>(Config::DEFAULT_TICK_RATE)));

    clock.setTickRate(128);
    check("a project's rate becomes the fixed step", nearly(clock.getFixedStep(), 1.0f / 128.0f));

    // A hand-edited project.json is the reason for the bounds: a rate of zero
    // is a step of zero, and a step of zero is a tick loop that never drains.
    clock.setTickRate(0);
    check("  zero is clamped rather than dividing by nothing",
          clock.getFixedStep() > 0.0f
       && nearly(clock.getFixedStep(), 1.0f / static_cast<float>(Config::MIN_TICK_RATE)));

    clock.setTickRate(100000);
    check("  and an absurd rate is clamped too",
          nearly(clock.getFixedStep(), 1.0f / static_cast<float>(Config::MAX_TICK_RATE)));

    // The accumulator is a duration, so a faster project buys more ticks per
    // hitch rather than a longer stall.
    clock.setTickRate(64);
    const int budget = static_cast<int>(Config::MAX_FRAME_ACCUMULATOR / clock.getFixedStep());
    check("  the hitch budget is ticks, not seconds of stall", budget >= 16);
}

void testACommandedStepIsDeliveredWhole() {
    std::printf("A step that was asked for:\n");

    // Rates whose fixed step is not a binary fraction, which is most of them.
    // Handing the accumulator n * step seconds and letting it divide them back
    // out loses one at rate after rate: it need only come back a hair short.
    const uint32_t rates[] = { 60u, 64u, 100u, 128u, 250u };

    for (const uint32_t rate : rates) {
        Clock clock;
        clock.setTickRate(rate);
        clock.setPaused(true);
        clock.beginFrame();          // starts the clock; no wall time has passed

        clock.requestStep(20);
        clock.beginFrame();

        int delivered = 0;
        while (clock.consumeFixedStep()) ++delivered;

        char label[64];
        std::snprintf(label, sizeof(label), "  twenty asked at %u Hz is twenty run", rate);
        check(label, delivered == 20);
    }

    // The tick number is the count of steps handed out, so it cannot drift from
    // the loop it describes: there is one place that hands one out.
    Clock counted;
    counted.setTickRate(64);
    counted.setPaused(true);
    counted.beginFrame();

    check("no tick has run yet", counted.getTick() == 0);

    counted.requestStep(3);
    counted.beginFrame();
    uint32_t ran = 0;
    while (counted.consumeFixedStep()) ++ran;

    check("three steps run three ticks", ran == 3);
    check("  and the clock counted them", counted.getTick() == 3);

    // Paused with nothing asked for: no time accrues, so no tick runs.
    counted.beginFrame();
    check("a paused clock hands out nothing", !counted.consumeFixedStep());
    check("  and the tick stands still", counted.getTick() == 3);

    // A step survives the play state changing under it. It was asked for; which
    // way pause moved afterwards is not a reason to lose it.
    counted.requestStep(2);
    counted.setPaused(false);
    counted.setPaused(true);
    counted.beginFrame();
    ran = 0;
    while (counted.consumeFixedStep()) ++ran;
    check("a queued step outlives a pause toggle", ran == 2);
}

void testInputCommand() {
    std::printf("Per-tick input command:\n");

    InputMap map;
    check("an undefined action has no command slot", map.indexOf("Nothing") < 0);

    map.define("Move/Forward", { InputBinding{InputSource::Key, 87, 1.0f} });
    map.define("Jump",         { InputBinding{InputSource::Key, 32, 1.0f} });
    check("defining an action gives it a slot", map.indexOf("Move/Forward") == 0);
    check("  and the next one the next slot", map.indexOf("Jump") == 1);

    // Stable for the session: a replayed command has to mean what it meant when
    // it was recorded, which it cannot if a slot moved under it.
    map.addBinding("Move/Forward", InputBinding{InputSource::Key, 265, 1.0f});
    map.define("Move/Forward", { InputBinding{InputSource::Key, 87, 1.0f} });
    check("  and redefining an action keeps it", map.indexOf("Move/Forward") == 0);

    // Before the first tick a reader gets "nothing held" rather than whatever
    // the last session left, so a system that runs early is not fed stale input.
    check("the pre-tick command is zeroed",
          map.command().sequence == 0 && map.command().pressed == 0
       && nearly(map.command().axis[0], 0.0f));

    map.beginTick(7);
    const InputCommand first = map.command();
    check("a built command carries the tick it drives", first.tick == 7);
    check("  and a sequence number of its own", first.sequence == 1);

    map.beginTick(8);
    check("the next tick is a new command", map.command().tick == 8
       && map.command().sequence == 2);

    // Sequence and tick are separate because a replay re-runs old ticks: the
    // tick repeats, the sequence that acknowledged it does not.
    check("  so sequence and tick are not the same number",
          map.command().sequence != map.command().tick);

    // An action past the cap is still readable at frame rate; it just has no
    // room in a command, which is a warning rather than a failure.
    for (int i = 0; i < static_cast<int>(MAX_INPUT_ACTIONS) + 4; ++i) {
        map.define("Filler" + std::to_string(i), { InputBinding{InputSource::Key, 100 + i, 1.0f} });
    }
    check("an action past the command cap has no slot",
          map.indexOf("Filler" + std::to_string(MAX_INPUT_ACTIONS + 3)) < 0);
    check("  while the ones that fit kept theirs", map.indexOf("Move/Forward") == 0);
}

// The unit a tick consumes. Input arrives on the frame clock and simulation
// runs on the tick clock, so a fixed update reading frame state directly would
// drop a tap taken between two ticks and repeat a press across every tick of a
// slow frame; the command is built per tick to close both.
// The latch surviving a frame with no tick, and draining so a second tick does
// not re-see a press, are not covered: both need key events, and key state
// reaches the engine only through GLFW callbacks.
// A command has to say everything a tick did with it. Axes alone do not: a
// character steers relative to a view, the view turns on the render clock, and
// a tick that asks the scene for it reads whatever the last frame left there.
void testCommandCarriesTheView() {
    std::printf("A command carries the view it was aimed with:\n");

    InputMap map;
    const glm::quat aimed  = glm::angleAxis(glm::radians(90.0f),  Math::WORLD_UP);
    const glm::quat turned = glm::angleAxis(glm::radians(-30.0f), Math::WORLD_UP);

    map.setView(aimed);
    map.beginTick(1);
    check("the tick's command takes the view it was built with",
          glm::all(glm::epsilonEqual(map.command().view, aimed, 1e-6f)));

    // The frame turns the camera again, which is what a frame does. The tick
    // already running must not follow it, or replaying its command walks
    // somewhere the original did not.
    map.setView(turned);
    check("  and does not follow the view once it is built",
          glm::all(glm::epsilonEqual(map.command().view, aimed, 1e-6f)));

    map.beginTick(2);
    check("  while the next tick takes the view as it now stands",
          glm::all(glm::epsilonEqual(map.command().view, turned, 1e-6f)));
}

} // namespace

void runCoreTests() {
    testMathConvention();
    testTickRate();
    testACommandedStepIsDeliveredWhole();
    testInputCommand();
    testCommandCarriesTheView();
}
