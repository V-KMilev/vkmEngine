// Debug drawing: how long a drawing stands, what goes past the cap, and that every thread's drawing shows.

#include <cstdint>
#include <thread>
#include <vector>

#include "debug/draw/debug_draw.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

constexpr float    FRAME = 1.0f / 60.0f;
constexpr uint64_t WORLD = 0;  ///< A world never replaced: a new Scene's epoch, as a new DebugDraw takes it.

/// A segment told apart by @p kind and @p index: from (kind, index, 0), one meter along X.
void mark(DebugDraw& draw, float kind, float index, const DebugStyle& style = {}) {
    draw.line({kind, index, 0.0f}, {kind + 1.0f, index, 0.0f}, style);
}

/// How many of the frame's segments are of @p kind.
size_t countOf(const DebugDraw& draw, float kind) {
    size_t count = 0;
    for (const DebugLine& line : draw.lines()) count += line.a.x == kind;
    return count;
}

/// A frame: collect, then end.
void frame(DebugDraw& draw, float seconds = FRAME, uint64_t world = WORLD) {
    draw.collect(seconds, world);
    draw.endFrame();
}

} // namespace

TEST("a frame's drawing stands for that frame, a tick's until the next tick, a timed one for its game seconds") {
    DebugDraw draw;
    const float OWN = 1.0f, TICKED = 2.0f, TIMED = 3.0f;

    mark(draw, OWN, 0.0f);
    draw.beginTick();
    mark(draw, TICKED, 0.0f);
    draw.endTick();
    mark(draw, TIMED, 0.0f, DebugStyle().lasting(1.0f));
    draw.collect(0.5f, WORLD);
    CHECK(countOf(draw, OWN) == 1u);
    CHECK(countOf(draw, TICKED) == 1u);
    CHECK(countOf(draw, TIMED) == 1u);
    draw.endFrame();

    // No tick in between: the tick's stands; half its second gone, the timed one too.
    draw.collect(0.5f, WORLD);
    CHECK(countOf(draw, OWN) == 0u);
    CHECK(countOf(draw, TICKED) == 1u);
    CHECK(countOf(draw, TIMED) == 1u);
    draw.endFrame();

    // A tick that draws nothing replaces it; the second is spent.
    draw.beginTick();
    draw.endTick();
    draw.collect(0.5f, WORLD);
    CHECK(draw.lines().empty());
    draw.endFrame();

    // A frame nothing collects shows nothing, and its own drawing goes with it.
    mark(draw, OWN, 1.0f);
    draw.endFrame();
    frame(draw);
    CHECK(draw.lines().empty());
}

TEST("past the cap a frame drops what stood for seconds, then the last tick's, then its own oldest, and says so once") {
    DebugDraw draw;
    const float TIMED = 1.0f, TICKED = 2.0f, OWN = 3.0f;
    const uint32_t MAX = DebugDraw::MAX_LINES;

    for (int i = 0; i < 10; ++i) mark(draw, TIMED, static_cast<float>(i), DebugStyle().lasting(10.0f));
    draw.beginTick();
    for (int i = 0; i < 10; ++i) mark(draw, TICKED, static_cast<float>(i));
    draw.endTick();
    frame(draw);
    CHECK(draw.lines().size() == 20u);

    // 15 over: the ten timed, then five of the tick's.
    for (uint32_t i = 0; i < MAX - 5; ++i) mark(draw, OWN, static_cast<float>(i));
    draw.collect(FRAME, WORLD);
    CHECK(draw.lines().size() == MAX);
    CHECK(countOf(draw, TIMED) == 0u);
    CHECK(countOf(draw, TICKED) == 5u);
    CHECK(countOf(draw, OWN) == MAX - 5);
    CHECK(draw.dropped() == 15u);
    draw.endFrame();

    // 8 over: the tick's last five, then the frame's own first three.
    for (uint32_t i = 0; i < MAX + 3; ++i) mark(draw, OWN, static_cast<float>(i));
    draw.collect(FRAME, WORLD);
    CHECK(draw.lines().size() == MAX);
    CHECK(countOf(draw, TICKED) == 0u);
    CHECK(draw.lines().front().a.y == 3.0f);
    CHECK(draw.dropped() == 23u);
    draw.endFrame();
    CHECK(countInLog("debug lines in a frame") == 1);
}

TEST("what other threads draw before the frame collects shows with the frame's own") {
    DebugDraw draw;
    mark(draw, 1.0f, 0.0f);
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&draw, t] {
            for (int i = 0; i < 100; ++i) mark(draw, 2.0f + static_cast<float>(t), static_cast<float>(i));
        });
    }
    for (std::thread& worker : workers) worker.join();
    draw.collect(FRAME, WORLD);
    CHECK(draw.lines().size() == 401u);
    for (int t = 0; t < 4; ++t) CHECK(countOf(draw, 2.0f + static_cast<float>(t)) == 100u);
}

TEST("a replaced world takes what stood in it along: the timed, the last tick's and this frame's timed") {
    DebugDraw draw;
    mark(draw, 1.0f, 0.0f, DebugStyle().lasting(10.0f));
    draw.beginTick();
    mark(draw, 2.0f, 0.0f);
    draw.endTick();
    frame(draw);
    CHECK(draw.lines().size() == 2u);

    // Given seconds this frame, in the world it replaced; the frame's own still shows.
    mark(draw, 3.0f, 0.0f, DebugStyle().lasting(10.0f));
    mark(draw, 4.0f, 0.0f);
    draw.collect(FRAME, WORLD + 1);
    CHECK(draw.lines().size() == 1u);
    CHECK(countOf(draw, 4.0f) == 1u);
}
