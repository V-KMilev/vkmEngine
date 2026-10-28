#include "support.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "debug/debug_draw.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/light.h"
#include "platform/threading/thread_pool.h"
#include "system/debug/debug_view_system.h"
#include "system/visibility/visibility.h"

namespace {

// What a frame shows once its drawings are collected, @p simSeconds of the game's time on, in
// the world @p epoch names.
const std::vector<DebugLine>& collected(
    DebugDraw& draw,
    float simSeconds = 1.0f / 60.0f,
    uint64_t epoch = 0
) {
    draw.collect(1.0f / 60.0f, simSeconds, epoch);
    return draw.lines();
}

bool sameColor(uint32_t packed, const glm::vec4& color) {
    return glm::length(glm::unpackUnorm4x8(packed) - color) < 1e-2f;
}

bool allIn(const std::vector<DebugLine>& lines, const glm::vec4& color) {
    const auto inIt = [&](const DebugLine& line) { return sameColor(line.color, color); };
    return !lines.empty() && std::all_of(lines.begin(), lines.end(), inIt);
}

glm::vec3 start(const DebugLine& line) { return glm::vec3(line.a); }
glm::vec3 end(const DebugLine& line) { return glm::vec3(line.b); }

// The whole call is the shape, where and how big, and a color: a color is a style.
void testAColorIsAWholeStyle() {
    std::printf("What a color makes of a style:\n");

    DebugDraw draw;
    draw.sphere(glm::vec3(1.0f, 2.0f, 3.0f), 2.0f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    const std::vector<DebugLine>& lines = collected(draw);
    check("a sphere is three great circles", lines.size() == 3u * DebugDraw::CIRCLE_SEGMENTS);
    check("  in the color it was given", allIn(lines, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)));
    const bool onTheSphere = std::all_of(lines.begin(), lines.end(), [](const DebugLine& line) {
        return nearly(glm::distance(start(line), glm::vec3(1.0f, 2.0f, 3.0f)), 2.0f);
    });
    check("  every point of it on the sphere", onTheSphere);
    check(
        "  seen where seen and ghosted where hidden",
        std::all_of(lines.begin(), lines.end(), [](const DebugLine& line) {
            return (line.flags & 3u) == static_cast<uint32_t>(DebugDepth::Ghosted);
        })
    );

    // No color of its own takes its category's.
    draw.setShown(DebugCategory::Colliders, true);
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f), DebugStyle(DebugCategory::Colliders).wide(3.0f).onTop());
    const std::vector<DebugLine>& own = collected(draw);
    const glm::vec4 colliders = DebugPalette::of(DebugCategory::Colliders).value();
    const uint32_t  onTop     = static_cast<uint32_t>(DebugDepth::OnTop);
    check("a line of a category alone is in its color", allIn(own, colliders));
    check("  as wide as asked", own.size() == 1 && nearly(own[0].width, 3.0f));
    check("  over everything", own.size() == 1 && (own[0].flags & 3u) == onTop);

    // An RGB color, which glm could take for a rotation too.
    draw.box(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    check("a box takes an RGB color", allIn(collected(draw), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)));
    draw.box(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.5f));
    check("  and an RGBA one", allIn(collected(draw), glm::vec4(0.0f, 0.0f, 1.0f, 0.5f)));
}

void testACategoryDrawsOnlyWhileShown() {
    std::printf("What a category off draws:\n");

    DebugDraw draw;
    check("gameplay's own lines show from the start", draw.shown(DebugCategory::General));
    check("  and no view of the scene's does", !draw.shown(DebugCategory::Colliders));

    draw.line(glm::vec3(0.0f), glm::vec3(1.0f), DebugCategory::Colliders);
    check("a line of a category off is not drawn", collected(draw).empty());

    draw.setShown(DebugCategory::Colliders, true);
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f), DebugCategory::Colliders);
    check("  and is once it is on", collected(draw).size() == 1);

    draw.setShown(DebugCategory::General, false);
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f));
    check("gameplay's own lines go when General is off", collected(draw).empty());
    check("  the others' stay on", draw.shown(DebugCategory::Colliders));

    // A tick's line and a timed one stand: the game paused, say, with nothing to replace them.
    draw.beginTick();
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f), DebugCategory::Colliders);
    draw.endTick();
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f), DebugStyle(DebugCategory::Colliders).lasting(5.0f));
    check("what stands in it shows", collected(draw, 0.0f).size() == 2);
    draw.setShown(DebugCategory::Colliders, false);
    check("  and hides once it is off", collected(draw, 0.0f).empty());
    draw.setShown(DebugCategory::Colliders, true);
    check("  to show again once it is back on", collected(draw, 0.0f).size() == 2);
}

void testEachShapeIsItsSegments() {
    std::printf("What each shape is drawn as:\n");

    DebugDraw draw;
    const auto count = [&](auto&& shape) {
        shape();
        return collected(draw).size();
    };
    const glm::vec3 o(0.0f);
    const glm::vec3 path[4] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    const size_t    arc     = DebugDraw::CIRCLE_SEGMENTS / 2;

    check("a line is one segment", count([&] { draw.line(o, glm::vec3(1.0f)); }) == 1);
    check("a path of four points is three", count([&] { draw.polyline(path, 4); }) == 3);
    check("  and four when closed", count([&] { draw.polyline(path, 4, {}, true); }) == 4);
    check("an arrow is its shaft and four barbs", count([&] { draw.arrow(o, glm::vec3(0, 0, 2)); }) == 5);
    check("a box is its twelve edges", count([&] { draw.box(o, glm::vec3(1.0f)); }) == 12);
    check("  as is a frustum", count([&] { draw.frustum(glm::mat4(1.0f)); }) == 12);
    check("a half turn is half a circle's segments", count([&] {
        draw.arc(o, Math::WORLD_AXIS_X, Math::WORLD_AXIS_Y, 1.0f, 0.0f, glm::pi<float>());
    }) == arc);
    const size_t capsuleSegments = 2 * DebugDraw::CIRCLE_SEGMENTS + 4 + 4 * arc;
    check(
        "a capsule is two rings, four sides and four half turns",
        count([&] { draw.capsule(o, glm::vec3(0, 2, 0), 0.5f); }) == capsuleSegments
    );
    check("a cone is its rim and four sides", count([&] {
        draw.cone(o, glm::vec3(0, 0, -2), 0.5f);
    }) == DebugDraw::CIRCLE_SEGMENTS + 4);

    // Where they are, not only how many.
    draw.box(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    const std::vector<DebugLine> box = collected(draw);
    int along[3] = {0, 0, 0};
    for (const DebugLine& edge : box) {
        const glm::vec3 run = glm::abs(end(edge) - start(edge));
        for (int axis = 0; axis < 3; ++axis) {
            if (nearly(run[axis], 2.0f * glm::vec3(1.0f, 2.0f, 3.0f)[axis])) ++along[axis];
        }
    }
    const bool fourEach = along[0] == 4 && along[1] == 4 && along[2] == 4;
    check("  a box's edges run four along each axis, its full width", fourEach);

    draw.frustum(glm::mat4(1.0f));
    const std::vector<DebugLine> clip = collected(draw);
    const auto onTheCube = [](const DebugLine& edge) {
        const glm::vec3 a = glm::abs(start(edge));
        return nearly(a.x, 1.0f) && nearly(a.y, 1.0f) && nearly(a.z, 1.0f);
    };
    check("  the identity's frustum is the clip cube", std::all_of(clip.begin(), clip.end(), onTheCube));

    draw.capsule(glm::vec3(0, 1, 0), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), 0.5f, 1.0f);
    const std::vector<DebugLine> capsule = collected(draw);
    const auto offTheSegment = [](const DebugLine& wire) {
        const glm::vec3 p = start(wire);
        const glm::vec3 nearest(0.0f, glm::clamp(p.y, 0.0f, 2.0f), 0.0f);
        return nearly(glm::distance(p, nearest), 0.5f);
    };
    check(
        "  a capsule's every point is its radius off its segment",
        std::all_of(capsule.begin(), capsule.end(), offTheSegment)
    );

    draw.point(glm::vec3(1.0f), DebugStyle().wide(2.0f));
    const std::vector<DebugLine> dot = collected(draw);
    const bool round = dot.size() == 1 && start(dot[0]) == end(dot[0]);
    check(
        "a point is a segment ending where it starts, its widths across",
        round && nearly(dot[0].width, 2.0f * DebugDraw::POINT_WIDTHS)
    );

    draw.axes(glm::mat4(1.0f), 2.0f);
    const std::vector<DebugLine> axes = collected(draw);
    bool colored = axes.size() == 3;
    for (size_t axis = 0; colored && axis < 3; ++axis) {
        const Math::AxisColor c = Math::AXIS_COLORS[axis];
        colored = sameColor(axes[axis].color, DebugPalette::Color{c.r, c.g, c.b, 255}.value())
            && nearly(glm::length(end(axes[axis])), 2.0f);
    }
    check("a frame's axes are its three, in the axes' colors, as long as asked", colored);
}

void testAPulseKnowsHowFarAlongItIs() {
    std::printf("What a pulsed path tells the backend:\n");

    DebugDraw  draw;
    DebugPulse pulse;
    pulse.dash    = 0.5f;
    pulse.spacing = 2.0f;
    pulse.speed   = 6.0f;
    pulse.phase   = 1.0f;
    pulse.trail   = 0.2f;
    const std::vector<glm::vec3> path = {{0, 0, 0}, {3, 0, 0}, {3, 4, 0}};
    draw.pulse(path, glm::vec4(0.2f, 0.8f, 1.0f, 1.0f), pulse);
    const std::vector<DebugLine>& lines = collected(draw);

    check("a path of three points is two segments", lines.size() == 2);
    const bool along = lines.size() == 2
        && nearly(lines[0].a.w, 0.0f)
        && nearly(lines[0].b.w, 3.0f)
        && nearly(lines[1].a.w, 3.0f)
        && nearly(lines[1].b.w, 7.0f);
    check("  each end as far along the path as it is", along);
    check(
        "  with the dash, spacing, speed and phase",
        lines.size() == 2 && lines[1].pulse == glm::vec4(0.5f, 2.0f, 6.0f, 1.0f)
    );
    const bool trailed = lines.size() == 2 && ((lines[0].flags >> 8) & 255u) == 51u;
    check("  and the trail in its flags' second byte", trailed);

    draw.line(glm::vec3(0.0f), glm::vec3(1.0f));
    const std::vector<DebugLine>& solid = collected(draw);
    check("a line unpulsed has no spacing: solid", solid.size() == 1 && solid[0].pulse.y == 0.0f);
}

// A fixed update may run twice in a frame or not at all: what it draws must neither double
// nor flicker.
void testATickDrawsUntilTheNextTickDraws() {
    std::printf("What a tick's drawings stand for:\n");

    DebugDraw draw;
    draw.beginTick();
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f));
    draw.endTick();
    check("a tick's line is drawn", collected(draw).size() == 1);
    check("  and still on a frame no tick ran", collected(draw).size() == 1);

    draw.beginTick();
    draw.line(glm::vec3(2.0f), glm::vec3(3.0f));
    draw.endTick();
    draw.beginTick();
    draw.line(glm::vec3(4.0f), glm::vec3(5.0f));
    draw.endTick();
    const std::vector<DebugLine>& twice = collected(draw);
    const bool last = twice.size() == 1 && start(twice[0]) == glm::vec3(4.0f);
    check("two ticks in a frame draw once: the last", last);

    draw.beginTick();
    draw.endTick();
    check("a tick drawing nothing takes the last one's away", collected(draw).empty());

    draw.line(glm::vec3(0.0f), glm::vec3(1.0f));
    check("a frame's own line is drawn", collected(draw).size() == 1);
    check("  for that frame alone", collected(draw).empty());

    // A slow frame catches up on ticks: each a busy one, together far past the cap.
    const uint32_t busy = DebugDraw::MAX_LINES / 4;
    for (int tick = 0; tick < 30; ++tick) {
        draw.beginTick();
        for (uint32_t i = 0; i < busy; ++i) draw.line(glm::vec3(float(tick)), glm::vec3(1.0f));
        draw.endTick();
    }
    const uint64_t before = draw.dropped();
    const std::vector<DebugLine>& caught = collected(draw);
    check("a frame of many ticks draws the last one's whole", caught.size() == busy);
    check("  of the last tick", !caught.empty() && nearly(caught.front().a.x, 29.0f));
    check("  and drops nothing it would have drawn", draw.dropped() == before);
}

void testTimedLinesStandTheirSecondsOfGameTime() {
    std::printf("What a line given seconds stands for:\n");

    DebugDraw draw;
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f), DebugStyle(glm::vec3(1.0f)).lasting(1.0f));
    // Its first frame's time passed before it was drawn.
    check("a second's line shows its first frame", collected(draw, 0.4f).size() == 1);
    check("  and the next", collected(draw, 0.5f).size() == 1);
    check("  as long as the game stands still", collected(draw, 0.0f).size() == 1);
    check("  to its last moment", collected(draw, 0.45f).size() == 1);
    check("  and not past it", collected(draw, 0.1f).empty());

    draw.line(glm::vec3(0.0f), glm::vec3(1.0f), DebugStyle(glm::vec3(1.0f)).lasting(5.0f));
    draw.beginTick();
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f));
    draw.endTick();
    check("a world's lines stand in it", collected(draw, 0.1f, 0).size() == 2);
    check("  and go with it when it is replaced", collected(draw, 0.1f, 1).empty());

    // Drawn in a frame whose world is replaced before its lines are collected: the old world's.
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f), DebugStyle(glm::vec3(1.0f)).lasting(5.0f));
    draw.beginTick();
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f));
    draw.endTick();
    check("  even those drawn in the frame it goes", collected(draw, 0.1f, 2).empty());
}

void testTheCapDropsTheOldestOnce() {
    std::printf("What a frame past the cap keeps:\n");

    DebugDraw draw;
    const uint32_t over = DebugDraw::MAX_LINES + 100;
    for (uint32_t i = 0; i < over; ++i) draw.line(glm::vec3(float(i), 0.0f, 0.0f), glm::vec3(1.0f));
    const std::vector<DebugLine>& capped = collected(draw);
    check("a frame draws the cap's worth", capped.size() == DebugDraw::MAX_LINES);
    check("  the newest: the first hundred dropped", !capped.empty() && nearly(capped.front().a.x, 100.0f));
    check("  and counted", draw.dropped() == 100);

    // What stood longest goes first.
    const DebugStyle standing = DebugStyle(glm::vec3(1.0f)).lasting(60.0f);
    for (int i = 0; i < 10; ++i) draw.line(glm::vec3(-1.0f), glm::vec3(1.0f), standing);
    collected(draw);
    for (uint32_t i = 0; i < DebugDraw::MAX_LINES; ++i) draw.line(glm::vec3(float(i)), glm::vec3(1.0f));
    const std::vector<DebugLine>& fresh = collected(draw);
    check(
        "lines standing for seconds go before the frame's",
        fresh.size() == DebugDraw::MAX_LINES && nearly(fresh.front().a.x, 0.0f)
    );

    // A drawer nothing collects is bounded too.
    const uint64_t before = draw.dropped();
    for (uint32_t i = 0; i < 4 * DebugDraw::MAX_LINES; ++i) draw.line(glm::vec3(float(i)), glm::vec3(1.0f));
    const std::vector<DebugLine>& runaway = collected(draw);
    check("a runaway loop keeps the cap's worth", runaway.size() == DebugDraw::MAX_LINES);
    const float newest = float(4 * DebugDraw::MAX_LINES - 1);
    check("  its newest", !runaway.empty() && nearly(runaway.back().a.x, newest));
    check("  the rest counted dropped", draw.dropped() - before == 3ull * DebugDraw::MAX_LINES);
}

void testEveryThreadsLinesAreDrawn() {
    std::printf("What lines drawn from many threads come to:\n");

    DebugDraw draw;
    constexpr int THREADS = 4;
    constexpr int EACH    = 1000;
    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&draw, t] {
            for (int i = 0; i < EACH; ++i) draw.line(glm::vec3(float(t), float(i), 0.0f), glm::vec3(0.0f));
        });
    }
    for (std::thread& thread : threads) thread.join();
    const std::vector<DebugLine>& lines = collected(draw);
    check("every thread's every line is drawn", lines.size() == THREADS * EACH);

    bool inOrder = true;
    int  next[THREADS] = {};
    for (const DebugLine& line : lines) {
        const int t = static_cast<int>(line.a.x);
        inOrder = inOrder && t >= 0 && t < THREADS && static_cast<int>(line.a.y) == next[t]++;
    }
    check("  each thread's in the order it drew them", inOrder);

    // The engine's own workers, as a system on the job pool draws.
    parallelFor(EACH, 16, [&](size_t i) { draw.point(glm::vec3(float(i), 0.0f, 0.0f)); });
    const std::vector<DebugLine>& pooled = collected(draw);
    std::vector<bool> seen(EACH, false);
    for (const DebugLine& line : pooled) seen[static_cast<size_t>(line.a.x)] = true;
    check(
        "the job pool's are all drawn, once",
        pooled.size() == EACH && std::all_of(seen.begin(), seen.end(), [](bool s) { return s; })
    );
}

void testAMutedOrInertDrawDrawsNothing() {
    std::printf("What draws nothing:\n");

    DebugDraw draw;
    draw.setMuted(true);
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f));
    draw.setMuted(false);
    check("a replayed tick draws nothing", collected(draw).empty());

    DebugDraw& none = DebugDraw::inert();
    none.setShown(DebugCategory::General, true);
    none.setMuted(false);
    none.sphere(glm::vec3(0.0f), 1.0f, glm::vec3(1.0f));
    check("a host showing no frames draws nothing whatever it is told", collected(none).empty());
}

void testWhatNoFrameShowedIsLetGo() {
    std::printf("What a frame nothing collected leaves:\n");

    DebugDraw draw;
    draw.line(glm::vec3(0.0f), glm::vec3(1.0f));
    draw.beginTick();
    draw.line(glm::vec3(2.0f), glm::vec3(3.0f));
    draw.endTick();
    draw.endFrame();
    const std::vector<DebugLine>& left = collected(draw);
    check("a frame's own line goes with it", left.size() == 1);
    check("  its tick's waits for a frame that shows", left.size() == 1 && start(left[0]) == glm::vec3(2.0f));
    draw.endFrame();

    // The editor draws after the frame is collected: its lines are the next frame's.
    collected(draw);
    draw.line(glm::vec3(5.0f), glm::vec3(6.0f));
    draw.endFrame();
    const std::vector<DebugLine>& late = collected(draw);
    const bool next = late.size() == 2 && start(late[1]) == glm::vec3(5.0f);
    check("a line drawn after the collect is the next frame's", next);
}

// What DebugViewSystem draws needs a frame with a camera the scene is seen from.
struct ViewedScene {
    Scene           scene;
    DebugDraw       draw;
    Visibility      seen;
    DebugViewSystem view;

    std::vector<DebugLine> frame(TestFrame& test) {
        test.ctx.debug      = &draw;
        test.ctx.visibility = &seen;
        view.update(test.ctx);
        return collected(draw);
    }

    ViewedScene() {
        seen.hasCamera = true;
        seen.camera    = CameraData::from(
            glm::lookAt(glm::vec3(0, 2, 10), glm::vec3(0.0f), Math::WORLD_UP),
            glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f),
            glm::vec3(0, 2, 10)
        );
    }
};

void testTheViewDrawsTheSceneByCategory() {
    std::printf("What the scene's view draws:\n");

    ViewedScene s;
    const EntityId crate = s.scene.createEntity();
    Transform at;
    at.position = glm::vec3(1.0f, 0.0f, 0.0f);
    s.scene.add(crate, std::move(at));
    Collider collider;
    collider.parts[0].shape       = ColliderShape::Box;
    collider.parts[0].halfExtents = glm::vec3(0.5f);
    s.scene.add(crate, std::move(collider));
    TestFrame test(s.scene);

    check("a collider draws nothing while colliders are off", s.frame(test).empty());
    s.draw.setShown(DebugCategory::Colliders, true);
    const std::vector<DebugLine> box = s.frame(test);
    check("  its box's edges once they are on", box.size() == 12);
    check("  in the colliders' color", allIn(box, DebugPalette::of(DebugCategory::Colliders).value()));
    check("  round where it stands", !box.empty() && nearly(std::abs(box[0].a.x - 1.0f), 0.5f));

    DebugFocus focus;
    focus.entities = {crate};
    focus.primary  = crate;
    s.draw.setFocus(focus);
    check("the focus draws in the highlight", allIn(s.frame(test), DebugPalette::HIGHLIGHT.value()));

    s.scene.get<Collider>(crate).enabled = false;
    check("a collider turned off draws nothing", s.frame(test).empty());

    constexpr size_t TRIANGLES = 3000;
    const EntityId   rock      = s.scene.createEntity();
    s.scene.add(rock, Transform{});
    Collider mesh;
    mesh.parts[0].shape = ColliderShape::Mesh;
    for (size_t t = 0; t < TRIANGLES; ++t) {
        const float x = 0.001f * static_cast<float>(t);
        for (const glm::vec3& p : {glm::vec3(x, 0, 0), glm::vec3(x, 1, 0), glm::vec3(x + 1, 0, 0)}) {
            mesh.meshPoints.push_back(p);
        }
    }
    s.scene.add(rock, std::move(mesh));
    const size_t drawn = s.frame(test).size() / 3;
    check("a mesh of many triangles draws a share of them", drawn > 0 && drawn < TRIANGLES);
}

// An area light lights the side its -forward faces; a joint connected to nothing, or to itself, is
// off and draws no rope.
void testTheViewDrawsLightsAndJointsAsTheyWork() {
    std::printf("Which way an area light faces, and which joints hold:\n");

    ViewedScene s;
    const EntityId lamp = s.scene.createEntity();
    s.scene.add(lamp, Transform{});
    Light panel;
    panel.type = LightType::Rect;
    s.scene.add(lamp, std::move(panel));
    TestFrame test(s.scene);
    s.draw.setShown(DebugCategory::Lights, true);
    const glm::vec3 lit = -Math::computeForward(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) * 0.5f;
    bool toLit  = false;
    bool toDark = false;
    for (const DebugLine& line : s.frame(test)) {
        if (glm::length(start(line)) > 1e-4f) continue;
        toLit  = toLit || glm::distance(end(line), lit) < 1e-4f;
        toDark = toDark || glm::distance(end(line), -lit) < 1e-4f;
    }
    check("a one-sided panel's arrow points to the side it lights", toLit && !toDark);
    s.draw.setShown(DebugCategory::Lights, false);

    const EntityId hinge = s.scene.createEntity();
    s.scene.add(hinge, Transform{});
    const EntityId post = s.scene.createEntity();
    Transform away;
    away.position = glm::vec3(2.0f, 0.0f, 0.0f);
    s.scene.add(post, away);
    Joint joint;
    joint.connected       = hinge;
    joint.connectedAnchor = glm::vec3(1.0f, 0.0f, 0.0f);  // a rope to itself would show
    s.scene.add(hinge, joint);
    s.draw.setShown(DebugCategory::Colliders, true);
    const auto longest = [&] {
        float most = 0.0f;
        for (const DebugLine& line : s.frame(test)) {
            most = std::max(most, glm::distance(start(line), end(line)));
        }
        return most;
    };
    check("a joint connected to itself draws no rope", longest() < 0.1f);
    s.scene.get<Joint>(hinge).connected = post;
    check("  one connected to another does", longest() > 1.9f);
}

void testTheFocusAloneDrawsItsReach() {
    std::printf("Whose reach is drawn:\n");

    ViewedScene s;
    const EntityId lamp = s.scene.createEntity();
    s.scene.add(lamp, Transform{});
    Light light;
    light.type   = LightType::Point;
    light.radius = 4.0f;
    s.scene.add(lamp, std::move(light));
    TestFrame test(s.scene);
    s.draw.setShown(DebugCategory::Lights, true);

    const size_t sphere = 3u * DebugDraw::CIRCLE_SEGMENTS;
    check("a lamp with no focus asked draws its reach", s.frame(test).size() == sphere);

    DebugFocus focus;
    focus.detailForFocusOnly = true;
    s.draw.setFocus(focus);
    check("  and not while the focus alone draws reach", s.frame(test).empty());

    focus.entities = {lamp};
    s.draw.setFocus(focus);
    const std::vector<DebugLine> reach = s.frame(test);
    check("  but does in the focus", reach.size() == sphere);
    check("  in the highlight", allIn(reach, DebugPalette::HIGHLIGHT.value()));
}

void testTheEyeDrawsNoFrustum() {
    std::printf("Which cameras draw their frustum:\n");

    ViewedScene s;
    const EntityId eye   = s.scene.createEntity();
    const EntityId other = s.scene.createEntity();
    for (EntityId id : {eye, other}) {
        s.scene.add(id, Transform{});
        s.scene.add(id, Camera{});
    }
    TestFrame test(s.scene);
    s.draw.setShown(DebugCategory::Cameras, true);
    s.seen.cameraEntity = eye;

    // Its four lines to the eye, the near face, the four edges out, the far face and the up tab.
    const size_t frustum = 4 + 4 + 4 + 4 + 3;
    check("a camera draws its frustum, the one seen through does not", s.frame(test).size() == frustum);
}

void testBoundsAndTheFocusAreTheDrawnBoxes() {
    std::printf("Which boxes the bounds and the focus draw:\n");

    ViewedScene s;
    const EntityId a = s.scene.createEntity();
    const EntityId b = s.scene.createEntity();
    s.seen.objects.bounds  = {{glm::vec3(0.0f), glm::vec3(1.0f)}, {glm::vec3(2.0f), glm::vec3(3.0f)}};
    s.seen.objects.visible = {0, 1};
    s.seen.entities        = {a, b};
    TestFrame test(s.scene);

    s.draw.setShown(DebugCategory::Bounds, true);
    const std::vector<DebugLine> bounds = s.frame(test);
    check("every drawn mesh's box", bounds.size() == 24);
    check("  in the bounds' color", allIn(bounds, DebugPalette::of(DebugCategory::Bounds).value()));

    s.draw.setShown(DebugCategory::Bounds, false);
    s.draw.setShown(DebugCategory::Selection, true);
    check("no focus, no boxes", s.frame(test).empty());
    DebugFocus focus;
    focus.entities = {b, a};
    focus.primary  = b;
    s.draw.setFocus(focus);
    const std::vector<DebugLine> chosen = s.frame(test);
    const auto highlit = std::count_if(chosen.begin(), chosen.end(), [](const DebugLine& line) {
        return sameColor(line.color, DebugPalette::HIGHLIGHT.value());
    });
    const auto others = std::count_if(chosen.begin(), chosen.end(), [](const DebugLine& line) {
        return sameColor(line.color, DebugPalette::SECONDARY.value());
    });
    check("the focus's boxes: its primary in the highlight", chosen.size() == 24 && highlit == 12);
    check("  its others behind it", others == 12);
}

} // namespace

void runDebugTests() {
    testAColorIsAWholeStyle();
    testACategoryDrawsOnlyWhileShown();
    testEachShapeIsItsSegments();
    testAPulseKnowsHowFarAlongItIs();
    testATickDrawsUntilTheNextTickDraws();
    testTimedLinesStandTheirSecondsOfGameTime();
    testTheCapDropsTheOldestOnce();
    testEveryThreadsLinesAreDrawn();
    testAMutedOrInertDrawDrawsNothing();
    testWhatNoFrameShowedIsLetGo();
    testTheViewDrawsTheSceneByCategory();
    testTheViewDrawsLightsAndJointsAsTheyWork();
    testTheFocusAloneDrawsItsReach();
    testTheEyeDrawsNoFrustum();
    testBoundsAndTheFocusAreTheDrawnBoxes();
}
