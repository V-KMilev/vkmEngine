#include "support.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "stb_image.h"
#include "stb_image_write.h"

#include "core/math/bounds.h"
#include "core/math/easing.h"
#include "io/json_file.h"
#include "io/project_paths.h"
#include "platform/window/window_manager.h"
#include "platform/process/child_process.h"
#include "platform/threading/thread_pool.h"
#include "platform/window/frame_limiter.h"
#include "system/render/exposure.h"
#include "system/render/render_backend.h"
#include "system/render/render_settings.h"
#include "system/render/render_system.h"
#include "system/visibility/visibility.h"

namespace {

// The axis convention, pinned so changing it is a decision: right-handed, +Y up,
// forward -Z - glm's own and the renderer's view space, so screen-right is plain +X.
//
// The first three assertions pin the convention; the rest hold under either and
// catch a half-finished change.
void testMathConvention() {
    std::printf("Math - the axis convention:\n");

    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 forward = Math::computeForward(identity);
    const glm::vec3 right   = Math::computeRight(identity);
    const glm::vec3 up      = Math::computeUp(identity);

    check("forward is -Z", sameDirection(forward, {0.0f, 0.0f, -1.0f}));
    check("up is +Y",      sameDirection(up,      {0.0f, 1.0f,  0.0f}));
    check("right is +X",   sameDirection(right,   {1.0f, 0.0f,  0.0f}));

    // Whatever forward means, the three must agree; a half-finished change shows here
    // rather than as a character walking sideways.
    check(
        "the basis is right-handed: right x up == -forward",
        sameDirection(glm::cross(right, up), -forward)
    );
    check(
        "  and orthogonal",
        std::fabs(glm::dot(right, up)) < 1e-3f
            && std::fabs(glm::dot(right, forward)) < 1e-3f
            && std::fabs(glm::dot(up, forward)) < 1e-3f
    );

    // A rotation built to face a direction must face it, whichever way forward points.
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

    // A quarter turn about up takes forward onto -right: turning left is positive
    // about +Y in a right-handed basis. A flipped rotation sign anywhere fails here.
    const glm::quat quarter = glm::angleAxis(glm::half_pi<float>(), up);
    check(
        "a quarter turn about up sends forward to -right",
        sameDirection(Math::computeForward(quarter), -right)
    );

    // A look control maps angles to a rotation and back, so the two must agree, or a
    // camera flies fine and jumps once something grabs one it had not moved.
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
            // A rising pitch must raise the view.
            if (pitchDeg > 0.0f && aimed.y <= 0.0f) pitchRaises = false;
            if (pitchDeg < 0.0f && aimed.y >= 0.0f) pitchRaises = false;
        }
    }
    check("angles round-trip through a direction and back", anglesRoundTrip);
    check("  and a rising pitch raises the view", pitchRaises);

    // Yaw zero must face forward, or every authored angle changes meaning.
    float zeroYaw = 0.0f;
    float zeroPitch = 0.0f;
    Math::toYawPitch(forward, zeroYaw, zeroPitch);
    check(
        "  with yaw and pitch zero looking straight ahead",
        std::fabs(zeroYaw) < 1e-3f && std::fabs(zeroPitch) < 1e-3f
    );

    check(
        "worldRotationOf recovers a rotation from its matrix",
        sameDirection(
            Math::computeForward(Math::worldRotationOf(glm::mat4_cast(quarter))),
            Math::computeForward(quarter)
        )
    );

    // A parent's world matrix carries its scale, and quat_cast of a scaled basis is not
    // a rotation: 16 degrees off for a quarter turn at scale 2.
    const glm::quat tilted = quarter * glm::angleAxis(glm::radians(30.0f), right);
    const glm::mat4 scaled = glm::mat4_cast(tilted) * glm::scale(glm::mat4(1.0f), {2.0f, 1.0f, 0.5f});
    const glm::quat recovered = Math::worldRotationOf(scaled);
    check(
        "  and from a scaled one, where quat_cast alone does not",
        sameDirection(Math::computeForward(recovered), Math::computeForward(tilted))
            && sameDirection(Math::computeUp(recovered), Math::computeUp(tilted))
    );
}

// Math::worldRotationOf and Transform::fromModelMatrix must agree; any extractor
// agrees on an ordinary matrix, and these two are not ordinary.
void testARotationReadFromAMatrixHasOneAnswer() {
    std::printf("A rotation read back out of a matrix:\n");

    const glm::quat turned = glm::angleAxis(glm::radians(40.0f), glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
    const glm::mat4 rotation = glm::mat4_cast(turned);

    const glm::mat4 mirrored = glm::scale(glm::mat4(1.0f), {-1.0f, 1.0f, 1.0f}) * rotation;
    const Transform decomposed = Transform::fromModelMatrix(mirrored);
    const glm::quat read = Math::worldRotationOf(mirrored);
    check(
        "under a mirrored parent, the pose and the transform agree",
        rotationErrorDegrees(read, decomposed.rotation) < 0.1f
    );

    const glm::mat4 rebuilt = Transform::computeModelMatrix(decomposed);
    bool same = true;
    for (int column = 0; column < 4; ++column) {
        same = same && glm::length(rebuilt[column] - mirrored[column]) < 1e-4f;
    }
    check("  on a rotation that rebuilds the matrix", same);

    const glm::mat4 flattened = rotation * glm::scale(glm::mat4(1.0f), {1.0f, 0.0f, 1.0f});
    const glm::quat flat = Math::worldRotationOf(flattened);
    check(
        "an axis scaled to nothing gives a number, not a NaN",
        std::isfinite(flat.w) && std::isfinite(flat.x) && std::isfinite(flat.y) && std::isfinite(flat.z)
    );
    check("  and the rotation the other two axes imply", rotationErrorDegrees(flat, turned) < 0.1f);
}

void testARayFindsTheLevelItPointsAt() {
    std::printf("A ray onto a level plane, and closing on a target at any frame rate:\n");
    const Math::Ray down{{1.0f, 5.0f, 2.0f}, glm::normalize(glm::vec3(1.0f, -1.0f, 0.0f))};
    const std::optional<glm::vec3> hit = Math::rayHitsLevel(down, 1.0f);
    const glm::vec3 under = {5.0f, 1.0f, 2.0f};
    check("a ray down meets the plane under it", hit && glm::length(*hit - under) < 1e-4f);
    check("  not one above it", !Math::rayHitsLevel(down, 6.0f));
    const Math::Ray level{{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    check("  and a level ray meets none", !Math::rayHitsLevel(level, 0.0f));

    // Closing two frames of dt as far as one frame of 2 dt.
    const float once  = damp(4.0f, 0.1f);
    const float twice = 1.0f - (1.0f - damp(4.0f, 0.05f)) * (1.0f - damp(4.0f, 0.05f));
    check("damp closes as far in two short frames as in one long one", std::abs(once - twice) < 1e-5f);
    check("  and none of the way in no time", damp(4.0f, 0.0f) == 0.0f);
}

// Auto-exposure's histogram, read: the stops that bring what is lit to the key, the shade and
// the sun left out, within the settings' range; then the adaptation toward them across frames.
void testAutoExposureAimsTheLitAtTheKey() {
    std::printf("Auto-exposure from a histogram of log2 luminance:\n");
    using namespace Exposure;
    RenderSettings settings;

    const bool even = binOf(MIN_LOG + 0.5f * BIN_STEP) == 1 && binOf(MAX_LOG - 0.01f) == BINS - 1;
    check("the bins split the range in log2", even);
    check("  the first taking the dark, the last the bright", binOf(-40.0f) == 0 && binOf(40.0f) == BINS - 1);
    check("  each centred where its centre says", binOf(binCentre(binOf(-3.0f))) == binOf(-3.0f));

    const int mid = binOf(-3.0f);
    std::vector<uint32_t> bins(BINS, 0u);
    bins[mid] = 1000;
    const float lit = std::log2(KEY) - binCentre(mid);
    const std::optional<float> one = targetStops(bins.data(), settings);
    check("a frame lit alike is brought to the key", one && std::abs(*one - lit) < 1e-4f);

    bins[binOf(6.0f)] = 40;
    bins[0]           = 100000;
    const std::optional<float> withSun = targetStops(bins.data(), settings);
    const bool unmoved = withSun && std::abs(*withSun - lit) < 1e-4f;
    check("  a sun under a twentieth of the frame and a black sky do not move it", unmoved);

    // Shade under LOW_SHARE of the lit: left out whole.
    bins[binOf(-9.0f)] = 600;
    const std::optional<float> shaded = targetStops(bins.data(), settings);
    check("  nor shade under the low share", shaded && std::abs(*shaded - lit) < 1e-4f);

    // Half shade, half lit: the low share's edge falls inside the shade's bin, so the part of it
    // above the edge counts.
    std::vector<uint32_t> split(BINS, 0u);
    split[binOf(-9.0f)] = 500;
    split[mid]          = 500;
    const float splitMean = (0.1f * binCentre(binOf(-9.0f)) + 0.45f * binCentre(mid)) / 0.55f;
    const std::optional<float> clipped = targetStops(split.data(), settings);
    const bool partInside = clipped && std::abs(*clipped - (std::log2(KEY) - splitMean)) < 1e-3f;
    check("  a bin the share splits counts its part inside", partInside);

    std::vector<uint32_t> dark(BINS, 0u);
    dark[0] = 5000;
    check("  a frame with nothing lit asks for nothing", !targetStops(dark.data(), settings));

    std::vector<uint32_t> night(BINS, 0u);
    night[binOf(-13.0f)] = 1000;
    const std::optional<float> held = targetStops(night.data(), settings);
    check("  a night past the range is brightened no further", held && *held == settings.exposureMaximum);

    const float darker   = adaptStops(2.0f, 0.0f, 0.1f, settings);
    const float brighter = adaptStops(0.0f, 2.0f, 0.1f, settings);
    check("the stops move toward the target", darker < 2.0f && darker > 0.0f && brighter > 0.0f);
    check("  darkening faster than brightening, by default", 2.0f - darker > brighter);
    const float closed = 2.0f * std::exp(-settings.adaptDarker * 0.1f);
    check("  closing the share a rate gives", std::abs(darker - closed) < 1e-5f);
    check("  and standing still for no time", adaptStops(1.0f, 3.0f, 0.0f, settings) == 1.0f);

    RenderSettings on;
    on.autoExposure = true;
    Adaptation eye;
    check("adapting, the authored exposure until a read", eye.update(nullptr, 0.1f, on) == on.exposure);
    const float day = *targetStops(bins.data(), on) + on.exposure;
    check("  the first read taken whole", std::abs(eye.update(bins.data(), 0.1f, on) - day) < 1e-5f);
    const float kept = eye.update(dark.data(), 0.1f, on);
    check("  a frame with nothing lit keeping it", std::abs(kept - day) < 1e-5f);
    const float nightStops = *targetStops(night.data(), on) + on.exposure;
    const float midway     = eye.update(night.data(), 0.1f, on);
    eye.forget();
    const bool  stayed     = std::abs(eye.update(nullptr, 0.1f, on) - midway) < 1e-5f;
    check("  forgotten midway, held until the next read", midway > kept && midway < nightStops && stayed);
    check("  which is taken whole", std::abs(eye.update(night.data(), 0.1f, on) - nightStops) < 1e-5f);
    const float later = eye.update(bins.data(), 0.1f, on);
    check("  and after it, adapted toward", later < nightStops && later > day);
    check("off, the authored exposure", eye.update(bins.data(), 0.1f, settings) == settings.exposure);
    check("  back on, the next read taken whole", std::abs(eye.update(bins.data(), 0.1f, on) - day) < 1e-5f);
}

// A project's tick rate is fixedUpdate's cadence and, networked, the wire's clock.
void testTickRate() {
    std::printf("Tick rate:\n");

    Clock clock;
    check(
        "a fresh clock ticks at the engine default",
        nearly(clock.getFixedStep(), 1.0f / static_cast<float>(Config::DEFAULT_TICK_RATE))
    );

    clock.setTickRate(128);
    check("a project's rate becomes the fixed step", nearly(clock.getFixedStep(), 1.0f / 128.0f));

    // Bounded for hand-edited project.json: rate zero is step zero, a tick loop that
    // never drains.
    clock.setTickRate(0);
    check(
        "  zero is clamped rather than dividing by nothing",
        clock.getFixedStep() > 0.0f
            && nearly(clock.getFixedStep(), 1.0f / static_cast<float>(Config::MIN_TICK_RATE))
    );

    clock.setTickRate(100000);
    check(
        "  and an absurd rate is clamped too",
        nearly(clock.getFixedStep(), 1.0f / static_cast<float>(Config::MAX_TICK_RATE))
    );

    // The accumulator is a duration, so a faster project buys more ticks per hitch,
    // not a longer stall.
    clock.setTickRate(64);
    const int budget = static_cast<int>(Config::MAX_FRAME_ACCUMULATOR / clock.getFixedStep());
    check("  the hitch budget is ticks, not seconds of stall", budget >= 16);
}

// A frame longer than the hitch budget. The accumulator refuses the excess, so the
// variable-step half must too, or particles and behaviours advance through a span
// physics never ran: capping the accumulator alone would move the simulation a
// quarter-second on a two-second stall and everything reading getSimDelta two seconds.
//
// Driven by time scale, not a real sleep: the scale multiplies the measured span
// before the cap, so a two-millisecond frame is hitch enough.
void testAHitchIsCappedForEveryReaderOfTheFrame() {
    std::printf("A frame longer than the hitch budget:\n");

    Clock clock;
    clock.beginFrame();                  // starts it; the first frame measures nothing
    clock.setTimeScale(1.0e6f);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    clock.beginFrame();

    check("the frame really was a hitch", clock.getDeltaTime() > 0.0f);
    check(
        "what the world advances by is capped",
        clock.getSimDelta() <= Config::MAX_FRAME_ACCUMULATOR + 1e-4f
    );

    int steps = 0;
    while (clock.consumeFixedStep()) ++steps;
    const float fixedSpan = static_cast<float>(steps) * clock.getFixedStep();

    check(
        "  and the fixed steps took that same span",
        fixedSpan <= clock.getSimDelta() + 1e-4f && clock.getSimDelta() - fixedSpan < clock.getFixedStep()
    );
}

// A press latched while nothing runs is dropped, not delivered on resume - and a time
// scale of zero runs no tick either, just like a paused clock.
void testAStillWorldIsStillWhicheverWayItWasStopped() {
    std::printf("Which frames leave the world standing still:\n");

    const auto stillAfterAFrame = [](auto&& setUp) {
        Clock clock;
        setUp(clock);
        clock.beginFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        clock.beginFrame();
        return clock.isSimulationStill();
    };

    check("a running clock moves the world", !stillAfterAFrame([](Clock&) {}));
    check("a paused one does not", stillAfterAFrame([](Clock& c) { c.setPaused(true); }));
    check("  nor does one at time scale zero", stillAfterAFrame([](Clock& c) { c.setTimeScale(0.0f); }));
    check("  nor one at pacing zero", stillAfterAFrame([](Clock& c) { c.setPacing(0.0f); }));
    check("a paused clock asked for a step moves it", !stillAfterAFrame([](Clock& c) {
        c.setPaused(true);
        c.requestStep(1);
    }));
}

void testACommandedStepIsDeliveredWhole() {
    std::printf("A step that was asked for:\n");

    // Rates whose fixed step is not a binary fraction, which is most. Dividing n * step
    // seconds back out of the accumulator loses one if it comes back a hair short.
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

    // The tick number counts steps handed out, from one place, so it cannot drift.
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

    // A requested step survives the play state changing under it.
    counted.requestStep(2);
    counted.setPaused(false);
    counted.setPaused(true);
    counted.beginFrame();
    ran = 0;
    while (counted.consumeFixedStep()) ++ran;
    check("a queued step outlives a pause toggle", ran == 2);
}

// A networked client runs ticks a few per cent fast or slow, so the server's queue of
// its commands neither runs dry nor backs up. Pacing bends how much wall time buys a
// tick, never the step: a replay re-runs the server's ticks, which a moving step would
// change. At one - every end but a playing client - the frame must be exactly unpaced.
void testPacingBendsHowOftenATickIsOwedNotItsLength() {
    std::printf("Pacing the clock:\n");

    Clock clock;
    clock.setTickRate(128);
    const float step = clock.getFixedStep();

    check("a fresh clock is paced at exactly one", clock.getPacing() == 1.0f);
    clock.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    clock.beginFrame();
    check(
        "  and at one the world advances by exactly the measured frame",
        clock.getSimDelta() == std::min(clock.getDeltaTime(), Config::MAX_FRAME_ACCUMULATOR)
    );

    clock.setPacing(1.05f);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    clock.beginFrame();
    check(
        "paced fast, the frame buys that much more simulation time",
        clock.getSimDelta() == std::min(clock.getDeltaTime() * 1.05f, Config::MAX_FRAME_ACCUMULATOR)
    );
    check("  and the step a tick advances by is untouched", clock.getFixedStep() == step);

    clock.setPacing(0.95f);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    clock.beginFrame();
    check(
        "paced slow, that much less",
        clock.getSimDelta() == std::min(clock.getDeltaTime() * 0.95f, Config::MAX_FRAME_ACCUMULATOR)
    );
    check("  with the step still untouched", clock.getFixedStep() == step);

    clock.setPacing(-1.0f);
    check("a negative pacing is clamped to a stop", clock.getPacing() == 0.0f);
}

// The unit a tick consumes. Input arrives on the frame clock and simulation on the
// tick clock, so reading frame state directly would drop a tap between ticks and repeat
// a press across a slow frame's ticks; a per-tick command closes both.

void testInputCommand() {
    std::printf("Per-tick input command:\n");

    InputMap map;
    check("an undefined action has no command slot", map.indexOf("Nothing") < 0);
    check("a new map holds the engine's UI click first", map.indexOf(InputActions::UI_CLICK) == 0);

    map.define("Move/Forward", { InputBinding{InputSource::Key, 87, 1.0f} });
    map.define("Jump",         { InputBinding{InputSource::Key, 32, 1.0f} });
    check("defining an action gives it a slot", map.indexOf("Move/Forward") == 1);
    check("  and the next one the next slot", map.indexOf("Jump") == 2);

    // Stable for the session: a replayed command must mean what it meant when recorded.
    map.rebind("Move/Forward", { InputBinding{InputSource::Key, 265, 1.0f} });
    map.define("Move/Forward", { InputBinding{InputSource::Key, 87, 1.0f} });
    check("  and redefining an action keeps it", map.indexOf("Move/Forward") == 1);

    // Before the first tick a reader gets "nothing held", not the last session's input.
    check(
        "the pre-tick command is zeroed",
        map.command().sequence == 0 && map.command().pressed == 0 && nearly(map.command().axis[0], 0.0f)
    );

    map.beginTick(7);
    const InputCommand first = map.command();
    check("a built command carries the tick it drives", first.tick == 7);
    check("  and a sequence number of its own", first.sequence == 1);

    map.beginTick(8);
    check("the next tick is a new command", map.command().tick == 8 && map.command().sequence == 2);

    // Separate because a replay re-runs old ticks: the tick repeats, the acknowledging
    // sequence does not.
    check("  so sequence and tick are not the same number", map.command().sequence != map.command().tick);

    // An action past the cap still reads at frame rate; it has no room in a command,
    // which warns rather than fails.
    for (int i = 0; i < static_cast<int>(MAX_INPUT_ACTIONS) + 4; ++i) {
        map.define("Filler" + std::to_string(i), { InputBinding{InputSource::Key, 100 + i, 1.0f} });
    }
    check(
        "an action past the command cap has no slot",
        map.indexOf("Filler" + std::to_string(MAX_INPUT_ACTIONS + 3)) < 0
    );
    check("  while the ones that fit kept theirs", map.indexOf("Move/Forward") == 1);
}

// The path a keypress takes: a device records a level, the map turns it into an
// action value and latches the edge, and beginTick drains that into the command.
void testAKeypressReachesAnAction() {
    std::printf("What a key does on its way to an action:\n");

    InputHandle device;
    InputMap    map;
    const HostChrome host;
    map.define("Jump",       { InputBinding{InputSource::Key, 32, 1.0f} });
    map.define(
        "Move/Right",
        { InputBinding{InputSource::Key, 68, 1.0f}, InputBinding{InputSource::Key, 65, -1.0f} }
    );

    map.update(device, host);
    check("nothing is held before anything is pressed", !map.held("Jump"));

    device.onKeyEvent(32, true);
    map.update(device, host);
    check("a key down reads as held", map.held("Jump"));
    check("  and as pressed on that frame", map.pressed("Jump"));
    check("  and not as released", !map.released("Jump"));

    map.update(device, host);
    check("it stays held while the key is", map.held("Jump"));
    check("  and the press edge is spent", !map.pressed("Jump"));

    device.onKeyEvent(32, false);
    map.update(device, host);
    check("letting go reads as released", map.released("Jump"));
    check("  and no longer held", !map.held("Jump"));

    device.onKeyEvent(68, true);
    map.update(device, host);
    check("one direction of an axis is full deflection", nearly(map.axis("Move/Right"), 1.0f));
    device.onKeyEvent(65, true);
    map.update(device, host);
    check("  and holding both cancels to nothing", nearly(map.axis("Move/Right"), 0.0f));

    // The pointer is not an action - no binding can express a position - so it travels
    // beside them, and the map derives the delta.
    device.moveTo(100.0, 40.0);
    device.addScroll(2.0);
    map.update(device, host);
    check(
        "the pointer arrives where the device put it",
        nearly(map.pointer().x, 100.0f) && nearly(map.pointer().y, 40.0f)
    );
    check(
        "  its movement is measured, not reported",
        nearly(map.pointerDelta().x, 100.0f) && nearly(map.pointerDelta().y, 40.0f)
    );
    check("  and the wheel carries the notches turned", nearly(map.wheel(), 2.0f));

    // Over a blocking element of the game's UI the wheel is the UI's, as a click would
    // be, so a list scrolling is not also a weapon switch.
    map.setPointerOverUI(true);
    map.update(device, host);
    check("over the game's UI, gameplay's wheel turns nothing", nearly(map.wheel(), 0.0f));
    check("  and the UI's carries every notch", nearly(map.uiWheel(), 2.0f));
    map.setPointerOverUI(false);
}

// A text field reads characters and editing keys in order, with Backspace repeating
// while held.
void testTypingArrivesAsTextAndRepeats() {
    std::printf("What typing gives a text field:\n");

    InputHandle device;
    InputMap    map;
    HostChrome  host;
    constexpr int BACKSPACE = 259;   // GLFW_KEY_BACKSPACE

    device.onText(U'h');
    device.onText(U'\u00E9');
    device.onKeyEvent(BACKSPACE, true);
    map.update(device, host);
    check("the characters typed arrive in order", map.text() == std::u32string_view(U"h\u00E9"));
    check("  and a key going down is typed", map.typed(BACKSPACE));

    map.update(device, host);
    check("the next frame has no characters left over", map.text().empty());
    check("  and a key only held is not typed again", !map.typed(BACKSPACE));

    // The window reports an auto-repeat as another press of a key that is down.
    device.onKeyEvent(BACKSPACE, true);
    map.update(device, host);
    check("a repeat types it again", map.typed(BACKSPACE));

    host.setCapture(false, true);
    device.onText(U'x');
    device.onKeyEvent(BACKSPACE, true);
    map.update(device, host);
    check(
        "while the host's field has the keyboard, nothing is typed here",
        map.text().empty() && !map.typed(BACKSPACE)
    );
}

// A game's player files go in a folder of the game's name, beside the engine's own, and a
// name cannot climb out of it.
void testAGameKeepsItsPlayerFilesInItsOwnFolder() {
    std::printf("Where a game keeps its player's files:\n");
#ifndef _WIN32
    const char*                 was  = std::getenv("XDG_CONFIG_HOME");
    const std::string           kept = was ? was : "";
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "vkm_player_root_test";
    std::filesystem::remove_all(base);
    ::setenv("XDG_CONFIG_HOME", base.c_str(), 1);

    const std::filesystem::path root = ProjectPaths::playerRoot("Some Game");
    check("in the platform's config folder, under the game's name", root == base / "Some Game");
    check("  made when asked for", std::filesystem::is_directory(root));
    check(
        "a name with a path in it keeps to its last part",
        ProjectPaths::playerRoot("../up/Other") == base / "Other"
    );
    const bool trailing = ProjectPaths::playerRoot("Some Game/") == root;
    check("  and one ending in a separator is the same folder", trailing);

    // Its own files: none yet is nothing, quietly; a damaged one is nothing, not the game's end.
    const std::filesystem::path own = root / "settings.json";
    check("a file not written yet reads as nothing", readOwnFile(own).empty());
    check("  one written", writeOwnFile(own, nlohmann::json{{"volume", 0.5}}));
    check("  reads back", readOwnFile(own).value("volume", 0.0) == 0.5);
    std::ofstream(own) << "{ not json";
    check("  and a damaged one reads as nothing", readOwnFile(own).empty());

    if (was) ::setenv("XDG_CONFIG_HOME", kept.c_str(), 1);
    else ::unsetenv("XDG_CONFIG_HOME");
    std::filesystem::remove_all(base);
#else
    std::printf("  (%%APPDATA%% on Windows is not changed by a test)\n");
#endif
}

// A controls screen listens for whatever goes down next, named for showing it; a repeat or a
// key only held is not a new press.
void testAPressIsHeardForRebinding() {
    std::printf("What a rebinding screen hears:\n");

    InputHandle device;
    InputMap    map;
    HostChrome  host;
    constexpr int W = 87, LEFT_SHIFT = 340;   // GLFW_KEY_W, GLFW_KEY_LEFT_SHIFT

    map.update(device, host);
    check("no press, nothing heard", !map.struck());

    device.onKeyEvent(LEFT_SHIFT, true);
    device.onKeyEvent(W, true);
    device.setButton(1, true);
    map.update(device, host);
    check(
        "keys before buttons, the lowest code first",
        map.struck() && map.struck()->source == InputSource::Key && map.struck()->code == W
    );
    check("  named as a player reads it", bindingName(*map.struck()) == "W");
    check("  and a shift by its side", bindingName({InputSource::Key, LEFT_SHIFT, 1.0f}) == "Left Shift");

    device.clearStrikes();
    device.onKeyEvent(W, true);
    map.update(device, host);
    check("a repeat is not heard again", !map.struck());

    device.clearStrikes();
    device.setButton(1, false);
    device.setButton(1, true);
    map.update(device, host);
    check(
        "a mouse button is heard",
        map.struck() && map.struck()->source == InputSource::MouseButton
            && bindingName(*map.struck()) == "Right mouse"
    );

    device.clearStrikes();
    device.setButton(1, false);
    device.setButton(0, true);
    host.setCapture(true, false);
    map.update(device, host);
    check("a click on the host's panel is not heard", !map.struck());
}

// A host's panels hold the devices while the author works in them, applied by the map:
// typing in the inspector cannot walk a character in Play, nor a panel click press a
// game button behind it.
void testWhatTheHostHoldsReadsAsUntouched() {
    std::printf("A device the host's UI is holding:\n");

    InputHandle device;
    InputMap    map;
    map.define("Jump", { InputBinding{InputSource::Key,         32, 1.0f} });
    map.define("Fire", { InputBinding{InputSource::MouseButton,  0, 1.0f} });

    HostChrome host;
    host.setCapture(false, true);
    device.onKeyEvent(32, true);
    device.setButton(0, true);
    map.update(device, host);
    check("a key typed into the host's field reads as up", !map.held("Jump"));
    check("  while the pointer, not held, still clicks", map.held("Fire"));

    map.beginTick(1);
    check("  and the tick sees no press of the key", !map.pressed(map.command(), "Jump"));

    host.setCapture(true, false);
    device.moveTo(50.0, 20.0);
    device.addScroll(1.0);
    map.update(device, host);
    check("a button held over the host's panel reads as released", map.released("Fire"));
    check("  the wheel turns nothing", nearly(map.wheel(), 0.0f));
    check(
        "  and the pointer's movement is not the game's",
        nearly(map.pointerDelta().x, 0.0f) && nearly(map.pointerDelta().y, 0.0f)
    );
    check(
        "  though where it is still travels",
        nearly(map.pointer().x, 50.0f) && nearly(map.pointer().y, 20.0f)
    );
    check(
        "a key the host saw go down stays the host's once it lets go",
        !map.held("Jump") && !map.pressed("Jump")
    );

    host.setCapture(false, false);
    map.update(device, host);
    check(
        "a button the host took mid-press stays up when it lets go",
        !map.held("Fire") && !map.pressed("Fire")
    );

    device.onKeyEvent(32, false);
    map.update(device, host);
    device.onKeyEvent(32, true);
    map.update(device, host);
    check("  and the next press of it is the game's", map.held("Jump") && map.pressed("Jump"));
}

// The editor runs many sessions against one map; neither a session's actions nor
// presses between sessions may reach the next one.
void testASessionLeavesTheMapAsItFoundIt() {
    std::printf("What one play session leaves the next:\n");

    InputHandle device;
    InputMap    map;
    const HostChrome host;
    map.reset();
    const uint32_t editorSlots = map.actionCount();

    // A click while paused: latched, then dropped by the tickless frame, as Engine::run does.
    device.setButton(0, true);
    map.update(device, host);
    map.discardPendingEdges();
    device.setButton(0, false);
    map.update(device, host);
    map.discardPendingEdges();
    map.beginTick(1);
    check(
        "a click made while paused never reaches a tick",
        !map.pressed(map.command(), InputActions::UI_CLICK)
            && !map.released(map.command(), InputActions::UI_CLICK)
    );

    for (int i = 0; i < static_cast<int>(MAX_INPUT_ACTIONS); ++i) {
        map.define("Session" + std::to_string(i), { InputBinding{InputSource::Key, 200 + i, 1.0f} });
    }
    check("a session can fill every command slot", map.actionCount() == MAX_INPUT_ACTIONS);

    map.reset();
    check(
        "the next session starts with only the defaults",
        map.actionCount() == editorSlots && map.bindings("Session0").empty()
    );
    check("  bound again", !map.bindings(InputActions::UI_CLICK).empty());

    map.define("Jump", { InputBinding{InputSource::Key, 32, 1.0f} });
    check("  and an action it defines has a slot", map.indexOf("Jump") >= 0);

    // Held on a panel through Stop, released with nothing bound: the next session's
    // first press of it is the game's.
    HostChrome panel;
    panel.setCapture(true, true);
    map.define("Sprint", { InputBinding{InputSource::Key, 340, 1.0f} });
    device.onKeyEvent(340, true);
    map.update(device, panel);
    map.reset();
    map.update(device, host);
    device.onKeyEvent(340, false);
    map.update(device, host);
    map.define("Sprint", { InputBinding{InputSource::Key, 340, 1.0f} });
    device.onKeyEvent(340, true);
    map.update(device, host);
    check("a key the host held through a reset is the game's next press", map.held("Sprint"));
}

// A tap beginning and ending between two ticks must survive the gap - the claim
// `docs/reference/input.md` makes for why a fixed update reads a command.
void testATapBetweenTwoTicksStillReachesOne() {
    std::printf("A tap that no tick was awake for:\n");

    InputHandle device;
    InputMap    map;
    const HostChrome host;
    map.define("Jump", { InputBinding{InputSource::Key, 32, 1.0f} });

    // Two frames, no tick between: down on the first, up on the second.
    device.onKeyEvent(32, true);
    map.update(device, host);
    device.onKeyEvent(32, false);
    map.update(device, host);

    map.beginTick(1);
    check("the tick sees the press it slept through", map.pressed(map.command(), "Jump"));
    check("  and the release with it", map.released(map.command(), "Jump"));
    check("  and reads the key as up, which it is", !map.held(map.command(), "Jump"));

    map.beginTick(2);
    check("the next tick does not see the same press again", !map.pressed(map.command(), "Jump"));
}

// One poll can deliver a press and its release together; a device keeping only the
// level would miss it. The strike recorded beside the level keeps a sub-frame tap.
void testATapInsideOneFrameIsStillSeen() {
    std::printf("A tap shorter than a frame:\n");

    InputHandle device;
    InputMap    map;
    const HostChrome host;
    map.define("Jump", { InputBinding{InputSource::Key,         32, 1.0f} });
    map.define("Fire", { InputBinding{InputSource::MouseButton,  0, 1.0f} });
    map.update(device, host);

    device.onKeyEvent(32, true);
    device.onKeyEvent(32, false);
    device.setButton(0, true);
    device.setButton(0, false);
    map.update(device, host);
    check("a key down and up before one sample reads as pressed", map.pressed("Jump"));
    check("  and so does a click", map.pressed("Fire"));

    map.update(device, host);
    check(
        "  and as released on the next, being up",
        map.released("Jump") && !map.held("Jump") && !map.pressed("Jump")
    );

    map.beginTick(1);
    check("the tick sees the press", map.pressed(map.command(), "Jump"));
    check("  and the release with it", map.released(map.command(), "Jump"));

    // A held key's repeats are presses of a key already down, not new strikes.
    device.onKeyEvent(32, true);
    map.update(device, host);
    device.onKeyEvent(32, true);
    map.update(device, host);
    check("a repeat of a held key is not another press", !map.pressed("Jump"));
}

// A controls screen rebinds an action and the settings file keeps it: only what the player
// changed, so a key left alone follows the game's default when that changes; loaded before the
// game defines its actions, a saved binding waits for the action to be defined.
void testARebindingIsSavedAndComesBack() {
    std::printf("A rebinding, saved and loaded:\n");
    InputMap map;
    map.define("Jump", {InputBinding{InputSource::Key, 32, 1.0f}});
    map.define("Dash", {InputBinding{InputSource::Key, 340, 1.0f}});
    check("nothing rebound, nothing saved", map.saveBindings().empty());

    map.rebind("Jump", {InputBinding{InputSource::MouseButton, 1, 1.0f}});
    const nlohmann::json saved = map.saveBindings();
    check("a rebinding is saved, alone", saved.size() == 1 && saved.contains("Jump"));

    InputMap next;
    next.loadBindings(saved);
    next.define("Jump", {InputBinding{InputSource::Key, 32, 1.0f}});
    next.define("Dash", {InputBinding{InputSource::Key, 340, 1.0f}});
    const std::vector<InputBinding>& jump = next.bindings("Jump");
    check(
        "  loaded before the action is defined, it takes its place",
        jump.size() == 1 && jump[0].source == InputSource::MouseButton && jump[0].code == 1
    );
    const std::vector<InputBinding>& dash = next.bindings("Dash");
    check("  the rest keep their defaults", dash.size() == 1 && dash[0].code == 340);

    next.rebind("Jump", {InputBinding{InputSource::Key, 32, 1.0f}});
    check("rebound back to its default, it is no longer saved", next.saveBindings().empty());

    next.rebind("Dash", {InputBinding{InputSource::Key, 70, 1.0f}});
    next.resetBindings();
    check(
        "reset, every action is its default again",
        next.bindings("Dash")[0].code == 340 && next.saveBindings().empty()
    );

    InputMap broken;
    broken.define("Jump", {InputBinding{InputSource::Key, 32, 1.0f}});
    broken.loadBindings(nlohmann::json::parse(R"({"Jump": "space", "Dash": [{"source": "Key", "code": 80}]})"));
    check("an entry that cannot be read is skipped", broken.bindings("Jump")[0].code == 32);
    check(
        "  and one for an action never defined is kept for the save",
        broken.saveBindings().contains("Dash")
    );
    // Each wrong somewhere inside its list: none may leave the action on key 0, unpressable.
    for (const char* wrong : {
             R"([{"code": "W"}])",
             R"([7])",
             R"([{"source": "Key", "code": 9999}])",
             R"([{"source": "Key", "code": -1}])",
             R"([{"source": "Pedal", "code": 3}])",
             R"([{"source": "Key", "code": 87}, {"source": "Key"}])"
         }) {
        InputMap map2;
        map2.define("Jump", {InputBinding{InputSource::Key, 32, 1.0f}});
        map2.loadBindings(nlohmann::json{{"Jump", nlohmann::json::parse(wrong)}});
        const std::vector<InputBinding>& kept = map2.bindings("Jump");
        check(
            (std::string("  wrong inside its list, ") + wrong + ", it keeps its own").c_str(),
            kept.size() == 1 && kept[0].code == 32 && map2.saveBindings().empty()
        );
    }

    // What a controls screen adds or clears is the player's too.
    InputMap edited;
    edited.define("Jump", {InputBinding{InputSource::Key, 32, 1.0f}});
    edited.define("Dash", {InputBinding{InputSource::Key, 340, 1.0f}});
    edited.rebind(
        "Jump",
        { InputBinding{InputSource::Key, 32, 1.0f}, InputBinding{InputSource::Key, 87, 1.0f} }
    );
    edited.rebind("Dash", {});
    const nlohmann::json kept = edited.saveBindings();
    check(
        "a binding added or cleared is saved",
        kept.contains("Jump") && kept.contains("Dash") && kept["Dash"].empty()
    );
}

// A tick asks a command for an action by name as a frame does, instead of reaching
// into the axis array and the two bitfields itself.
void testACommandIsReadByName() {
    std::printf("Asking a command for an action by name:\n");

    InputMap map;
    map.define(
        "Move/Right",
        { InputBinding{InputSource::Key, 68, 1.0f}, InputBinding{InputSource::Key, 65, -1.0f} }
    );
    map.define("Jump",       { InputBinding{InputSource::Key, 32,  1.0f} });
    map.define("Walk",       { InputBinding{InputSource::Key, 340, 1.0f} });

    // Built by hand: a peer's command is read by the same call. What a device does to a
    // map is testAKeypressReachesAnAction's question.
    InputCommand command;
    command.axis[static_cast<size_t>(map.indexOf("Move/Right"))] = -1.0f;
    command.axis[static_cast<size_t>(map.indexOf("Walk"))]       =  1.0f;
    command.pressed  = uint32_t(1) << map.indexOf("Jump");
    command.released = uint32_t(1) << map.indexOf("Walk");

    check("an axis comes back under its own name", nearly(map.axis(command, "Move/Right"), -1.0f));
    check("a press edge does too", map.pressed(command, "Jump"));
    check("  and a release edge", map.released(command, "Walk"));

    // held() uses the frame queries' threshold, so a caller never restates a constant
    // the map owns.
    check("held() reads an axis at full deflection as down", map.held(command, "Walk"));
    check("  and an untouched action as up", !map.held(command, "Jump"));
    check("  by magnitude, so a negative axis is held too", map.held(command, "Move/Right"));

    // An undefined action and one past the cap both read inactive rather than indexing
    // with -1.
    check(
        "an undefined action reads as nothing",
        nearly(map.axis(command, "NoSuchAction"), 0.0f)
            && !map.held(command, "NoSuchAction")
            && !map.pressed(command, "NoSuchAction")
            && !map.released(command, "NoSuchAction")
    );

    for (int i = 0; i < static_cast<int>(MAX_INPUT_ACTIONS) + 2; ++i) {
        map.define("Pad" + std::to_string(i), { InputBinding{InputSource::Key, 200 + i, 1.0f} });
    }
    const std::string past = "Pad" + std::to_string(MAX_INPUT_ACTIONS + 1);
    check("  and so does one past the command cap", map.indexOf(past) < 0 && !map.pressed(command, past));
}

// A game's bytes are held as the view is: latched into each tick's command until changed, never
// cut short, and gone with the session.
void testACommandCarriesTheGamesBytes() {
    std::printf("A command carries the game's own bytes:\n");

    InputMap map;
    const uint8_t move[] = {4, 9, 1};
    map.setPayload(move, sizeof(move));
    map.beginTick(1);
    const InputCommand& first = map.command();
    const bool same = std::equal(std::begin(move), std::end(move), first.payload.begin());
    check("the tick's command takes the bytes the game set", first.payloadSize == sizeof(move) && same);
    map.beginTick(2);
    check("  and the next keeps them, held until changed", map.command().payloadSize == sizeof(move));

    std::array<uint8_t, MAX_COMMAND_PAYLOAD + 4> long_{};
    long_.fill(7);
    const bool refused = !map.setPayload(long_.data(), long_.size());
    map.beginTick(3);
    const bool kept = map.command().payloadSize == sizeof(move);
    check("more than a command holds is refused, keeping what was held", refused && kept);

    map.reset();
    map.beginTick(4);
    check("  and none outlives the session", map.command().payloadSize == 0);
}

// A command must say everything a tick did with it: a character steers relative to a
// view turning on the render clock, and a tick asking the scene reads the last frame's.
void testCommandCarriesTheView() {
    std::printf("A command carries the view it was aimed with:\n");

    InputMap map;
    const glm::quat aimed  = glm::angleAxis(glm::radians(90.0f),  Math::WORLD_UP);
    const glm::quat turned = glm::angleAxis(glm::radians(-30.0f), Math::WORLD_UP);

    map.setView(aimed);
    map.beginTick(1);
    check(
        "the tick's command takes the view it was built with",
        glm::all(glm::epsilonEqual(map.command().view, aimed, 1e-6f))
    );

    // The frame turns the camera again; the running tick must not follow, or replaying
    // its command walks somewhere the original did not.
    map.setView(turned);
    check(
        "  and does not follow the view once it is built",
        glm::all(glm::epsilonEqual(map.command().view, aimed, 1e-6f))
    );

    map.beginTick(2);
    check(
        "  while the next tick takes the view as it now stands",
        glm::all(glm::epsilonEqual(map.command().view, turned, 1e-6f))
    );
}

// parallelFor keeps the first chunk and hands the rest to the pool as one batch whose
// indices workers claim. The grains below leave a short last chunk, a chunk per index,
// and no batch.
void testParallelForRunsEveryIndexOnce() {
    std::printf("What parallelFor hands the pool:\n");

    constexpr size_t COUNT = 4099;
    for (const size_t grain : {size_t(1), size_t(7), size_t(1000), COUNT * 2}) {
        std::vector<std::atomic<int>> hits(COUNT);
        parallelFor(COUNT, grain, [&](size_t i) { hits[i].fetch_add(1); });

        bool once = true;
        for (const std::atomic<int>& hit : hits) once = once && hit.load() == 1;
        std::printf("      grain %zu\n", grain);
        check("  every index runs exactly once", once);
    }
}

// A throwing chunk fails the whole call, whichever thread ran it; swallowed on a worker,
// the same bug would surface or vanish by which chunk it landed in.
void testAThrowFromAnyChunkReachesTheCaller() {
    std::printf("What parallelFor does with a chunk that throws:\n");

    constexpr size_t COUNT = 64;
    std::vector<std::atomic<int>> hits(COUNT);
    bool threw = false;
    try {
        parallelFor(COUNT, 1, [&](size_t i) {
            hits[i].fetch_add(1);
            if (i == COUNT - 1) throw std::runtime_error("last chunk");
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check("a queued chunk's throw reaches the caller", threw);

    bool once = true;
    for (const std::atomic<int>& hit : hits) once = once && hit.load() == 1;
    check("  after every other chunk still ran", once);

    threw = false;
    try {
        parallelFor(COUNT, 1, [&](size_t i) {
            if (i == 0) throw std::runtime_error("first chunk");
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check("the caller's own chunk's throw does too", threw);

    std::atomic<size_t> ran{0};
    parallelFor(COUNT, 1, [&](size_t) { ran.fetch_add(1); });
    check("  and the pool runs the next call whole", ran.load() == COUNT);
}

// With every worker busy elsewhere, the caller runs its queued chunks itself rather
// than sleeping until one frees.
void testTheCallerRunsWhatNoWorkerHasTaken() {
    std::printf("parallelFor with every worker busy elsewhere:\n");

    ThreadPool& pool = ThreadPool::get();
    const size_t workers = pool.threadCount();
    if (workers == 0) {
        std::printf("      no workers - skipped\n");
        return;
    }

    std::atomic<size_t> holding{0};
    std::atomic<bool>   release{false};
    for (size_t w = 0; w < workers; ++w) {
        pool.addTask([&] {
            holding.fetch_add(1);
            while (!release.load()) std::this_thread::yield();
        });
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (holding.load() < workers && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }

    // Release the workers eventually, so a regression fails rather than hangs the suite.
    std::thread releaser([&] {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        release.store(true);
    });

    constexpr size_t COUNT = 256;
    std::vector<std::atomic<int>> hits(COUNT);
    parallelFor(COUNT, 1, [&](size_t i) { hits[i].fetch_add(1); });
    const bool finishedFirst = !release.load();

    release.store(true);
    releaser.join();

    bool once = true;
    for (const std::atomic<int>& hit : hits) once = once && hit.load() == 1;
    check("every index runs exactly once", once);
    check("  and the call finishes while the workers are still busy", finishedFirst);
}

// A backend that draws nothing, reads back nothing and offers nothing.
struct NullBackend : RenderBackend {
    bool init(WindowManager& window) override { return true; }
    void render(const RenderView& view, const ResourceManager& resources) override {}

    bool readFrame(const RenderView& view, std::vector<uint8_t>& pixels) override {
        pixels.clear();
        return false;
    }
    uint32_t reloadChangedShaders() override { return 0; }
    uint32_t maxAnisotropy() const override { return 1; }
    bool holdsPixels(const Handle<TextureAsset>& texture, uint64_t version) const override { return false; }
    EditorRenderHooks* editorHooks() override { return nullptr; }
};

// A backend that reads back a made-up frame - a red top row over blue - all the
// screenshot path asks.
struct ReadBackBackend : NullBackend {
    bool readFrame(const RenderView& view, std::vector<uint8_t>& pixels) override {
        pixels.assign(static_cast<size_t>(view.viewportWidth) * view.viewportHeight * 3, 0);
        for (size_t texel = 0; texel < pixels.size() / 3; ++texel) {
            pixels[texel * 3 + (texel < view.viewportWidth ? 0 : 2)] = 255;
        }
        return true;
    }
};

// A screenshot is asked of the window by gameplay and made by the render path from the
// backend's frame: the window holds no image.
void testAScreenshotIsTheFrameTheBackendDrew() {
    std::printf("A screenshot, asked of the window:\n");

    Scene      scene;
    TestFrame  frame(scene);
    Visibility visibility;
    frame.chrome.setViewport(0, 0, 4, 3);
    frame.ctx.visibility = &visibility;

    RenderSystem render;
    render.setBackend(std::make_unique<ReadBackBackend>(), frame.window);

    const std::filesystem::path png = std::filesystem::temp_directory_path() / "vkm_screenshot_test.png";
    std::error_code ec;
    std::filesystem::remove(png, ec);

    render.update(frame.ctx);
    check("no request writes nothing", !std::filesystem::exists(png, ec));

    // Another writer's setting, which a screenshot must neither use nor change.
    const int otherLevel = stbi_write_png_compression_level;
    stbi_write_png_compression_level = 7;

    frame.window.saveScreenshot(png.string());
    render.update(frame.ctx);
    check("a request writes the frame drawn after it", std::filesystem::exists(png, ec));

    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    unsigned char* image = stbi_load(png.string().c_str(), &width, &height, &channels, 3);
    check("  at the viewport's size", image && width == 4 && height == 3);
    check(
        "  the top row first",
        image && image[0] == 255 && image[2] == 0 && image[(4 * 2) * 3] == 0 && image[(4 * 2) * 3 + 2] == 255
    );
    if (image) stbi_image_free(image);
    check("  leaving the PNG writer's settings as it found them", stbi_write_png_compression_level == 7);
    stbi_write_png_compression_level = otherLevel;
    check("  and using the request up", frame.window.takeScreenshotRequest().empty());

    std::filesystem::remove(png, ec);
    render.update(frame.ctx);
    check("  so the next frame writes nothing", !std::filesystem::exists(png, ec));
}

// A backend the device cannot run.
struct RefusingBackend : NullBackend {
    bool init(WindowManager& window) override { return false; }
};

// A backend that fails to init stops the host: kept, the window would run black.
void testABackendThatFailsToInitStopsTheHost() {
    std::printf("A backend that fails to init:\n");

    Scene        scene;
    TestFrame    frame(scene);
    RenderSystem render;

    bool threw = false;
    try {
        render.setBackend(std::make_unique<RefusingBackend>(), frame.window);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check("throws to the host", threw);
    check("  and is not installed", render.backend() == nullptr);
}

// Spelled the same as ecs_tests.cpp's own, and a different type: see
// testATypeIdIsTheTypesAlone there.
struct TypeIdProbe { int value = 0; };

// The present and events between one frame's wait and the next frame's start are
// inside the period, not added to it: a server capped at its tick rate must tick at it.
void testACappedLoopKeepsItsRate() {
    std::printf("A loop capped at 200 frames a second:\n");

    using namespace std::chrono;
    constexpr int FRAMES = 50;
    FrameLimiter limiter;
    limiter.setTargetFramerate(200);

    const auto start = steady_clock::now();
    for (int i = 0; i < FRAMES; ++i) {
        limiter.beginFrame();
        limiter.endFrame();
        // Work, not a sleep: a sleep is as coarse as the machine's scheduler tick.
        const auto workEnd = steady_clock::now() + milliseconds(2);
        while (steady_clock::now() < workEnd) {}
    }
    const double seconds = duration<double>(steady_clock::now() - start).count();
    std::printf("      %d frames in %.0f ms, where the cap allows %d\n", FRAMES, seconds * 1e3, FRAMES * 5);
    check("the time between frames does not lengthen each one", seconds < FRAMES * 0.006);
}

// The editor builds through a child process: what it prints must arrive, its exit code
// be told apart from success, and stop() must end it rather than wait it out.
void testAChildProcessIsHeardAndCanBeStopped() {
    std::printf("A program run in the background:\n");

#if defined(_WIN32)
    const std::filesystem::path shell = "C:\\Windows\\System32\\cmd.exe";
    const std::vector<std::string> says = {"/d", "/c", "echo one& echo two 1>&2& exit 3"};
    const std::vector<std::string> waits = {"/d", "/c", "ping -n 30 127.0.0.1 >NUL"};
#else
    const std::filesystem::path shell = "/bin/sh";
    const std::vector<std::string> says = {"-c", "echo one; echo two >&2; exit 3"};
    const std::vector<std::string> waits = {"-c", "sleep 30"};
#endif

    ChildProcess child;
    check("it starts", child.start(shell, says));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (child.running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const std::string output = child.takeOutput();
    check("  and ends on its own", !child.running());
    const bool both = output.find("one") != std::string::npos && output.find("two") != std::string::npos;
    check("  its output and its errors both arrive", both);
    check("  its exit code is kept", child.exitCode() == 3);
    check("  and output is handed over once", child.takeOutput().empty());

    check("a long one starts", child.start(shell, waits));
    const auto before = std::chrono::steady_clock::now();
    child.stop();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
    std::printf("      stopped in %.2f s\n", seconds);
    check("  and stop() ends it, not waits for it", !child.running() && seconds < 5.0);
    check("  and reads as stopped, not as succeeded", child.exitCode() == -1);
}

#if defined(__linux__)
// A host that dies - crashed, killed - takes what it started with it, as a Windows job does: a
// server a game hosted must not outlive the game.
void testAProgramEndsWhenItsHostDies() {
    std::printf("What a dead host's program does:\n");

    int fds[2];
    check("a pipe opens", pipe(fds) == 0);
    const pid_t host = fork();
    if (host == 0) {
        close(fds[0]);
        ChildProcess child;
        child.start("/bin/sh", {"-c", "echo $$; exec sleep 30"});
        std::string said;
        for (int i = 0; i < 500 && said.find('\n') == std::string::npos; ++i) {
            said += child.takeOutput();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const ssize_t sent = write(fds[1], said.data(), said.size());
        // Gone as a crash goes: no destructor stops what it started.
        _exit(sent > 0 ? 0 : 1);
    }
    close(fds[1]);
    char said[32] = {};
    const ssize_t got = read(fds[0], said, sizeof(said) - 1);
    close(fds[0]);
    int status = 0;
    waitpid(host, &status, 0);
    const pid_t program = got > 0 ? static_cast<pid_t>(std::atoi(said)) : 0;
    check("the host's program said who it is, and the host died", program > 0);

    bool gone = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (program > 0 && !gone && std::chrono::steady_clock::now() < deadline) {
        gone = kill(program, 0) != 0 && errno == ESRCH;
        if (!gone) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check("  and its program died with it", gone);
    if (program > 0 && !gone) kill(program, SIGKILL);
}
#endif

// A settings screen's render resolutions: of a monitor's video modes, those that fit on it, largest
// first, each once. GLFW lists a size once per colour depth and refresh rate, depth first, so its
// list repeats sizes and is not in size order; these modes do both, with two too large for the
// monitor and an empty one.
void testTheRenderResolutionsAreTheModesThatFit() {
    std::printf("The render resolutions a settings screen offers:\n");
    const std::vector<WindowSize> modes = {
        {1280, 720},
        {2560, 1440},
        {1920, 1080},
        {1280, 720},
        {1600, 900},
        {1920, 1200},
        {0, 0},
    };
    const std::vector<WindowSize> fit  = fittingWindowSizes(modes, {1920, 1080});
    const std::vector<WindowSize> want = {{1920, 1080}, {1600, 900}, {1280, 720}};
    check("the modes that fit on the monitor, largest first, each once", fit == want);
}

} // namespace

TypeId typeIdOfCoreTestsProbe() {
    return typeId<TypeIdProbe>();
}

// Spelled the same as ecs_tests.cpp's twin: a class local to a lambda a namespace-scope
// const holds. The const's internal linkage alone makes the class this file's own.
const auto TYPE_ID_OF_CONSTANTS_PROBE = [] {
    struct Probe { int value = 0; };
    return typeId<Probe>();
};

TypeId typeIdOfCoreTestsConstantsProbe() {
    return TYPE_ID_OF_CONSTANTS_PROBE();
}

void runCoreTests() {
    testMathConvention();
    testTheRenderResolutionsAreTheModesThatFit();
    testARayFindsTheLevelItPointsAt();
    testARotationReadFromAMatrixHasOneAnswer();
    testTickRate();
    testAutoExposureAimsTheLitAtTheKey();
    testAStillWorldIsStillWhicheverWayItWasStopped();
    testACommandedStepIsDeliveredWhole();
    testPacingBendsHowOftenATickIsOwedNotItsLength();
    testAHitchIsCappedForEveryReaderOfTheFrame();
    testInputCommand();
    testAKeypressReachesAnAction();
    testTypingArrivesAsTextAndRepeats();
    testAPressIsHeardForRebinding();
    testARebindingIsSavedAndComesBack();
    testAGameKeepsItsPlayerFilesInItsOwnFolder();
    testWhatTheHostHoldsReadsAsUntouched();
    testASessionLeavesTheMapAsItFoundIt();
    testATapBetweenTwoTicksStillReachesOne();
    testATapInsideOneFrameIsStillSeen();
    testParallelForRunsEveryIndexOnce();
    testAThrowFromAnyChunkReachesTheCaller();
    testTheCallerRunsWhatNoWorkerHasTaken();
    testACommandIsReadByName();
    testCommandCarriesTheView();
    testACommandCarriesTheGamesBytes();
    testAScreenshotIsTheFrameTheBackendDrew();
    testABackendThatFailsToInitStopsTheHost();
    testACappedLoopKeepsItsRate();
    testAChildProcessIsHeardAndCanBeStopped();
#if defined(__linux__)
    testAProgramEndsWhenItsHostDies();
#endif
}
