// The frame clock: how many ticks a frame runs, and how long a frame lasts, as the world sees them.
// Where a promise is about wall time, the test sleeps only to give a break room to show: what it
// checks holds however long the sleep took.

#include <algorithm>
#include <chrono>
#include <thread>

#include "core/clock.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

/// Open a frame on @p clock and take every tick it owes.
int frameTicks(Clock& clock) {
    clock.beginFrame();
    int ticks = 0;
    while (clock.consumeFixedStep()) ++ticks;
    return ticks;
}

} // namespace

TEST("a paused world runs no tick, a stepped one runs exactly its steps, and a held one waits") {
    Clock clock;
    clock.setTickRate(60);
    frameTicks(clock);
    clock.setPaused(true);

    sleepMs(50);
    CHECK(frameTicks(clock) == 0);
    CHECK(clock.getSimDelta() == 0.0f);

    clock.requestStep(3);
    sleepMs(50);
    CHECK(frameTicks(clock) == 3);
    CHECK(clock.getTick() == 3u);

    // A held world takes no step, a queued one included, until it is let go.
    clock.setHeld(true);
    clock.requestStep(2);
    CHECK(frameTicks(clock) == 0);
    clock.setHeld(false);
    CHECK(frameTicks(clock) == 2);
    CHECK(clock.getTick() == 5u);
}

TEST("the frame that lets a held world go lasts the steps asked meanwhile, which it runs") {
    Clock clock;
    clock.setTickRate(50);
    clock.setPaused(true);
    frameTicks(clock);

    clock.setHeld(true);
    clock.requestStep(3);
    CHECK(frameTicks(clock) == 0);

    // As Engine::run does: the frame opens held, and the hold is let go before its ticks.
    clock.beginFrame();
    clock.setHeld(false);
    CHECK_NEAR(clock.getSimDelta(), 3.0f * clock.getFixedStep(), 1e-6f);
    int ticks = 0;
    while (clock.consumeFixedStep()) ++ticks;
    CHECK(ticks == 3);
}

TEST("a frame stepped while paused lasts its steps, however long it took") {
    Clock clock;
    clock.setTickRate(50);
    clock.setPaused(true);
    frameTicks(clock);

    clock.requestStep(2);
    sleepMs(30);
    CHECK(frameTicks(clock) == 2);
    CHECK(clock.getDeltaTime() == 2.0f * clock.getFixedStep());
    CHECK(clock.getSimDelta() == 2.0f * clock.getFixedStep());

    // Not stepped, it is a frame of wall time in which the world stands still.
    sleepMs(5);
    CHECK(frameTicks(clock) == 0);
    CHECK(clock.getDeltaTime() > 0.0f);
    CHECK(clock.isSimulationStill());
}

TEST("the fixed step is the tick rate's, and the ticks keep pace with time whatever the frame rate") {
    Clock clock;
    clock.setTickRate(30);
    CHECK(clock.getFixedStep() == 1.0f / 30.0f);

    // Frames of uneven length: every tick is a step of the rate, and what is not yet a tick is
    // carried, so the ticks taken and the fraction left are the time that passed.
    frameTicks(clock);
    const int lengths[] = {3, 41, 7, 23, 2, 60, 11, 17, 5, 33};
    int ticks = 0;
    double passed = 0.0;
    for (const int ms : lengths) {
        sleepMs(ms);
        ticks  += frameTicks(clock);
        passed += clock.getSimDelta();
    }
    const double kept = (ticks + clock.getFixedAlpha()) * clock.getFixedStep();
    CHECK_NEAR(kept, passed, 1e-4f);
    CHECK(ticks > 0);

    // A rate past either end is held to the range the clock honors.
    clock.setTickRate(0);
    CHECK(clock.getFixedStep() == 1.0f / static_cast<float>(Config::MIN_TICK_RATE));
    clock.setTickRate(100000);
    CHECK(clock.getFixedStep() == 1.0f / static_cast<float>(Config::MAX_TICK_RATE));
}

TEST("a long hitch runs the capped ticks, not a spiral") {
    Clock clock;
    clock.setTickRate(60);
    frameTicks(clock);

    sleepMs(400);
    const int ticks = frameTicks(clock);
    CHECK(ticks * clock.getFixedStep() <= Config::MAX_FRAME_ACCUMULATOR + 1e-6f);
    CHECK(ticks >= static_cast<int>(Config::MAX_FRAME_ACCUMULATOR / clock.getFixedStep()) - 1);
    // The time past the cap is gone, not owed to the next frame.
    CHECK(clock.getFixedAlpha() < 1.0f);
}

TEST("time scale and the host's speed scale the ticks, and a shown menu holds them but for a step asked") {
    Clock clock;
    clock.setTickRate(60);
    frameTicks(clock);

    clock.setTimeScale(0.5f);
    clock.setHostScale(3.0f);
    sleepMs(10);
    frameTicks(clock);
    CHECK_NEAR(
        clock.getSimDelta(),
        std::min(clock.getDeltaTime() * 1.5f, Config::MAX_FRAME_ACCUMULATOR),
        1e-6f
    );

    clock.setTimeScale(0.0f);
    sleepMs(50);
    CHECK(frameTicks(clock) == 0);
    CHECK(clock.getSimDelta() == 0.0f);

    clock.setTimeScale(1.0f);
    clock.setHostScale(1.0f);
    clock.setMenuShown(true);
    sleepMs(50);
    CHECK(frameTicks(clock) == 0);
    clock.requestStep(1);
    CHECK(frameTicks(clock) == 1);
    clock.setMenuShown(false);
    sleepMs(50);
    CHECK(frameTicks(clock) > 0);
}
