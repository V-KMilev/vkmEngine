// Who casts which shadow: the key sun's cascades, and a spot's every caster.

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

TEST("of two suns, the one the scene keys casts the cascades, and the other lights without them") {
    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    addFloor(scene, shapes, material(screen.resources(), "test:floor", glm::vec3(0.3f), 0.9f));
    place(
        scene,
        shapes.cube,
        material(screen.resources(), "test:post", glm::vec3(0.3f)),
        {0.0f, 1.0f, 0.0f},
        {0.4f, 2.0f, 0.4f}
    );
    // The key, which the sky aims from the east, so its shadow falls west; the second from the west,
    // its shadow, were it cast, east.
    scene.environment().sky.sunAzimuth   = 90.0f;
    scene.environment().sky.sunElevation = 35.0f;
    addSun(scene);
    const EntityId other = scene.createEntity();
    Transform otherPose;
    otherPose.rotation = glm::quatLookAt(
        glm::normalize(glm::vec3(1.0f, -0.7f, 0.0f)),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    scene.add(other, otherPose);
    Light second;
    second.intensity   = 1.0f;
    second.castShadows = true;
    scene.add(other, second);
    const EntityId eye = addEye(scene, {0.0f, 4.0f, 7.0f}, {0.0f, 0.0f, 0.0f});

    const Picture frame = screen.shot();

    const glm::vec3 west = frame.mean(Rect::around(pixelOf(scene, eye, {-1.4f, 0.0f, 0.0f}), 3));
    const glm::vec3 east = frame.mean(Rect::around(pixelOf(scene, eye, {1.4f, 0.0f, 0.0f}), 3));
    const glm::vec3 open = frame.mean(Rect::around(pixelOf(scene, eye, {0.0f, 0.0f, 2.0f}), 3));
    std::printf("  west %.0f, east %.0f, open %.0f\n", luminance(west), luminance(east), luminance(open));
    CHECK(luminance(west) < 0.8f * luminance(open));
    CHECK(luminance(east) > 0.9f * luminance(open));
    // The second goes without by rule, not for want of room: no light is said to be over the budget.
    CHECK(countInLog("found no free atlas tile") == 0);
}

TEST("a spot's shadow holds every caster in its cone, and a fixed caster's stays when another moves") {
    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    scene.environment().sky.sunElevation = -30.0f;
    addFloor(scene, shapes, material(screen.resources(), "test:floor", glm::vec3(0.7f), 0.9f));
    // Slabs 2 m up around a spot 4 m up: each one's shadow on the floor twice as far out.
    const MaterialHandle slab = material(screen.resources(), "test:slab", glm::vec3(0.5f));
    const glm::vec2 fixedAt[] = {{-1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}};
    for (const glm::vec2& at : fixedAt) {
        const EntityId id = place(scene, shapes.cube, slab, {at.x, 2.0f, at.y}, {0.3f, 0.05f, 0.3f});
        scene.get<Mesh>(id).movable = false;
    }
    const EntityId mover = place(scene, shapes.cube, slab, {0.7f, 2.0f, 0.7f}, {0.3f, 0.05f, 0.3f});
    const EntityId spot  = scene.createEntity();
    Transform spotPose;
    spotPose.position = {0.0f, 4.0f, 0.0f};
    spotPose.rotation = glm::quatLookAt(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    scene.add(spot, spotPose);
    Light light = generateLight(LightType::Spot);
    light.intensity      = 80.0f;
    light.radius         = 10.0f;
    light.outerConeAngle = glm::radians(60.0f);
    light.innerConeAngle = glm::radians(50.0f);
    light.castShadows    = true;
    light.movable        = false;
    scene.add(spot, light);
    // From high above, so a slab and its shadow fall apart in the frame.
    const EntityId eye = addEye(scene, {0.0f, 12.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});

    const std::vector<Picture> frames = screen.shots({
        [&] { scene.get<Transform>(mover).position = {-0.7f, 2.0f, -0.7f}; },
    });

    REQUIRE(frames.size() == 2u);
    const auto shade = [&](const Picture& p, glm::vec2 at) {
        return luminance(p.mean(Rect::around(pixelOf(scene, eye, {2.0f * at.x, 0.0f, 2.0f * at.y}), 2)));
    };
    const float lit = luminance(frames[0].mean(Rect::around(pixelOf(scene, eye, {1.4f, 0.0f, -1.4f}), 2)));
    for (const Picture& p : frames) {
        for (const glm::vec2& at : fixedAt) CHECK(shade(p, at) < 0.5f * lit);
    }
    CHECK(shade(frames[0], {0.7f, 0.7f}) < 0.5f * lit);
    CHECK(shade(frames[1], {0.7f, 0.7f}) > 0.8f * lit);
    CHECK(shade(frames[1], {-0.7f, -0.7f}) < 0.5f * lit);
}
