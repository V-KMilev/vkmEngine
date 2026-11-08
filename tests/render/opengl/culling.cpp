// What the frame leaves out and what it swaps: a far level of detail, an object too small to see, what
// the GPU's occlusion cull hides, and a fixed mesh that moved anyway.

#include "ecs/component/render/lod.h"
#include "system/visibility/visibility_system.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

namespace {

/// The share of the box @p rect that is @p color's, a channel at a time: how much of it a shape fills.
float filled(const Picture& picture, const Rect& rect, bool (*of)(const glm::vec3&)) {
    int hits = 0, count = 0;
    for (uint32_t y = rect.y0; y < rect.y1; ++y) {
        for (uint32_t x = rect.x0; x < rect.x1; ++x) {
            hits += of(picture.at(x, y));
            ++count;
        }
    }
    return count ? static_cast<float>(hits) / static_cast<float>(count) : 0.0f;
}

/// The tightest rect holding every pixel @p of picks.
Rect bounds(const Picture& picture, bool (*of)(const glm::vec3&)) {
    Rect found{WIDTH, HEIGHT, 0, 0};
    for (uint32_t y = 0; y < picture.height; ++y) {
        for (uint32_t x = 0; x < picture.width; ++x) {
            if (!of(picture.at(x, y))) continue;
            found = {std::min(found.x0, x), std::min(found.y0, y), std::max(
                found.x1,
                x + 1
            ), std::max(found.y1, y + 1)};
        }
    }
    return found;
}

bool reddish(const glm::vec3& c) { return c.r > 2.0f * c.g && c.r > 2.0f * c.b && c.r > 40.0f; }

} // namespace

TEST("a level of detail draws its near mesh near and its far mesh far") {
    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    // A cube near, a sphere past 15 m, each red; seen square-on, a cube fills its box and a sphere a
    // quarter turn less.
    const EntityId object = place(
        scene,
        shapes.cube,
        material(screen.resources(), "test:red", {0.9f, 0.05f, 0.05f}),
        {0.0f, 2.0f, 0.0f},
        glm::vec3(4.0f)
    );
    LOD lod;
    lod.levels = {{shapes.cube, 15.0f}, {shapes.sphere, 1000.0f}};
    scene.add(object, lod);
    addSun(scene);
    const EntityId eye = addEye(scene, {0.0f, 2.0f, 8.0f}, {0.0f, 2.0f, 0.0f});

    const std::vector<Picture> frames = screen.shots({
        [&] { scene.get<Transform>(eye).position.z = 40.0f; },
    });

    REQUIRE(frames.size() == 2u);
    const float near = filled(frames[0], bounds(frames[0], reddish), reddish);
    const float far  = filled(frames[1], bounds(frames[1], reddish), reddish);
    std::printf("  near fills %.2f of its box, far %.2f\n", near, far);
    CHECK(near > 0.95f);
    CHECK(far < 0.85f);
    CHECK(far > 0.6f);
}

TEST("an object under three pixels across is not drawn, and one over it is") {
    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    // A glowing ball against a night sky: anything drawn of it shows.
    scene.environment().sky.sunElevation = -30.0f;
    MaterialAsset glow;
    glow.albedo   = glm::vec4(1.0f);
    glow.emission = glm::vec3(50.0f);
    const EntityId ball =
        place(scene, shapes.sphere, screen.resources().add(std::move(glow), "test:glow"), {0.0f, 0.0f, 0.0f});
    const EntityId eye = addEye(scene, {0.0f, 0.0f, 40.0f}, {0.0f, 0.0f, 0.0f});

    // At 150 m its box's sphere spans under 3 pixels; the ball itself still 2. Then the sky alone.
    const std::vector<Picture> frames = screen.shots({
        [&] { scene.get<Transform>(eye).position.z = 150.0f; },
        [&] { scene.get<Mesh>(ball).visible = false; },
    });

    REQUIRE(frames.size() == 3u);
    const Rect where = Rect::around({WIDTH / 2.0f, HEIGHT / 2.0f}, 8);
    const auto brightest = [&](const Picture& p) {
        float most = 0.0f;
        for (uint32_t y = where.y0; y < where.y1; ++y) {
            for (uint32_t x = where.x0; x < where.x1; ++x) most = std::max(most, luminance(p.at(x, y)));
        }
        return most;
    };
    std::printf(
        "  brightest near %.0f, far %.0f, with no ball %.0f\n",
        brightest(frames[0]),
        brightest(frames[1]),
        brightest(frames[2])
    );
    CHECK(brightest(frames[0]) > 200.0f);
    CHECK(brightest(frames[1]) < brightest(frames[2]) + 4.0f);
}

TEST("the GPU's occlusion cull draws the frame it would draw without it") {
    Screen screen;
    Scene& scene    = screen.scene();
    const Yard seen = yard(scene, screen.resources());
    const Shapes shapes(screen.resources());
    // A wall that hides most of a crowd of boxes, some showing past its ends and over its top.
    place(
        scene,
        shapes.cube,
        material(screen.resources(), "test:wall", {0.6f, 0.55f, 0.5f}),
        {0.0f, 1.0f, -2.0f},
        {5.0f, 2.0f, 0.3f}
    );
    const MaterialHandle green = material(screen.resources(), "test:green", {0.1f, 0.7f, 0.1f});
    for (int i = 0; i < 40; ++i) {
        const float x = -6.0f + 0.3f * static_cast<float>(i);
        place(
            scene,
            shapes.cube,
            green,
            {x, 0.4f + 0.06f * static_cast<float>(i % 7), -4.0f - 0.2f * (i % 5)},
            glm::vec3(0.5f)
        );
    }
    (void)seen;

    const std::vector<Picture> frames = screen.shots({
        [&] {
            RenderSettings settings = screen.graphics().settings();
            settings.occlusionCulling = true;
            screen.graphics().set(settings);
        },
    });

    REQUIRE(frames.size() == 2u);
    CHECK(!RenderSettings{}.occlusionCulling);
    CHECK(screen.graphics().settings().occlusionCulling);
    CHECK(differing(frames[0], frames[1]) < 0.001f);
}

TEST("a fixed mesh moved anyway is drawn where it went within the cull's stride, and said") {
    Screen screen;
    Scene& scene    = screen.scene();
    const Yard seen = yard(scene, screen.resources());
    scene.get<Mesh>(seen.red).movable = false;

    const std::vector<Picture> frames = screen.shots({
        [&] { scene.get<Transform>(seen.red).position.x = 0.0f; scene.get<Transform>(
            seen.sphere
        ).position.x = -1.6f; },
    }, static_cast<int>(VisibilitySystem::FIXED_CHECK_STRIDE) + 2);

    REQUIRE(frames.size() == 2u);
    const Rect middle = Rect::around(pixelOf(scene, seen.eye, {0.0f, 0.5f, 0.5f}), 3);
    const Rect left   = Rect::around(pixelOf(scene, seen.eye, {-1.6f, 0.5f, 0.5f}), 3);
    CHECK(reddish(frames[0].mean(left)));
    CHECK(reddish(frames[1].mean(middle)));
    CHECK(!reddish(frames[1].mean(left)));
    CHECK(countInLog("movable off moved") == 1);
}
