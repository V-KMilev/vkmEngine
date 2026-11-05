// What a first-person view holds: drawn through its own field of view, a mesh and a particle alike.

#include "ecs/component/effect/particle_emitter.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

namespace {

/// The mean place of the pixels @p of picks, and how many there are.
std::pair<glm::vec2, int> centroid(const Picture& picture, bool (*of)(const glm::vec3&)) {
    glm::vec2 sum(0.0f);
    int count = 0;
    for (uint32_t y = 0; y < picture.height; ++y) {
        for (uint32_t x = 0; x < picture.width; ++x) {
            if (!of(picture.at(x, y))) continue;
            sum += glm::vec2(static_cast<float>(x), static_cast<float>(y));
            ++count;
        }
    }
    return {count ? sum / static_cast<float>(count) : glm::vec2(-1.0f), count};
}

} // namespace

TEST("a viewmodel particle is drawn where a viewmodel mesh at its place is, through the viewmodel's field of view") {
    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    scene.environment().sky.sunElevation = -30.0f;  // a dark sky, against which both show
    const EntityId eye = addEye(scene, {0.0f, 1.6f, 5.0f}, {0.0f, 1.6f, 0.0f});
    scene.get<Camera>(eye).viewmodelFovY = glm::radians(40.0f);
    // Held 0.6 m ahead, right and low: unlike the world's 70 degrees, the viewmodel's 40 put it
    // further out from the middle.
    const glm::vec3 held = {0.15f, 1.5f, 4.4f};
    MaterialAsset red;
    red.type   = MaterialType::Unlit;
    red.albedo = {1.0f, 0.0f, 0.0f, 1.0f};
    const EntityId mesh =
        place(scene, shapes.cube, screen.resources().add(std::move(red), "test:red"), held, glm::vec3(0.02f));
    scene.get<Mesh>(mesh).seenBy = SeenBy::Viewmodel;

    const EntityId spark = scene.createEntity();
    Transform at;
    at.position = held;
    scene.add(spark, at);
    ParticleEmitter emitter;
    emitter.emitting     = false;
    emitter.lifetime     = {100.0f, 100.0f};
    emitter.speed        = {0.0f, 0.0f};
    emitter.acceleration = glm::vec3(0.0f);
    emitter.size         = {0.01f, 0.01f};
    emitter.sizeOverLife.keys.clear();
    emitter.color.keys   = {{0.0f, {0.0f, 1.0f, 0.0f, 1.0f}}};
    emitter.intensity    = 4.0f;
    emitter.seenBy       = SeenBy::Viewmodel;
    scene.add(spark, emitter);

    const std::vector<Picture> frames = screen.shots({
        [&] {
            scene.get<Mesh>(mesh).visible           = false;
            scene.get<ParticleEmitter>(spark).burst = 1;
        },
    });

    REQUIRE(frames.size() == 2u);
    const auto [cube, cubePixels] = centroid(
        frames[0],
        [](const glm::vec3& c) { return c.r > 100.0f && c.g < 60.0f; }
    );
    const auto [dot, dotPixels] = centroid(
        frames[1],
        [](const glm::vec3& c) { return c.g > 100.0f && c.r < 0.5f * c.g && c.b < 0.5f * c.g; }
    );
    const glm::vec2 world = pixelOf(scene, eye, held);
    std::printf(
        "  mesh at (%.1f, %.1f) over %d pixels, particle at (%.1f, %.1f) over %d, the world's lens (%.1f, %.1f)\n",
        cube.x,
        cube.y,
        cubePixels,
        dot.x,
        dot.y,
        dotPixels,
        world.x,
        world.y
    );
    REQUIRE(cubePixels > 0);
    REQUIRE(dotPixels > 0);
    CHECK_NEAR(dot, cube, 1.5f);
    CHECK(glm::length(cube - world) > 5.0f);
}
