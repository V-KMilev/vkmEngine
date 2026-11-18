// Rigid bodies through the engine as a game runs them: components in a scene, ticks on a pinned
// clock, poses read back.

#include "ecs/component/scene/world_transform.h"
#include "ecs/hierarchy_operations.h"

#include "harness/check.h"
#include "harness/world.h"
#include "physics/bodies.h"

using namespace Vkm::Test;

TEST("a dynamic body falls and comes to rest on a static floor") {
    Game game;
    addFloor(game.scene());
    const EntityId crate = addCrate(game.scene(), {0.0f, 3.0f, 0.0f});

    float lowest = 3.0f;
    game.run(240, [&](int) { lowest = std::min(lowest, positionOf(game.scene(), crate).y); });

    CHECK_NEAR(positionOf(game.scene(), crate), glm::vec3(0.0f, 0.5f, 0.0f), 0.01f);
    CHECK(glm::length(bodyOf(game.scene(), crate).linearVelocity) < 0.01f);
    CHECK(tiltDegrees(game.scene().get<Transform>(crate).rotation) < 0.5f);
    CHECK(lowest > 0.3f);
}

TEST("a body bounces by the larger restitution of the pair, and a slow touch does not bounce") {
    Game game;
    Scene& scene = game.scene();
    bodyOf(scene, addFloor(scene)).restitution = 0.0f;
    // A springy pad, for a dead ball.
    const EntityId pad = addBody(
        scene,
        {8.0f, 0.05f, 0.0f},
        box({1.0f, 0.05f, 1.0f}),
        RigidbodyMotion::Static
    );
    bodyOf(scene, pad).restitution = 0.8f;
    auto ball = [&](float x, float y, float restitution) {
        const EntityId id = addBody(scene, {x, y, 0.0f}, capsule(0.25f, 0.0f));
        bodyOf(scene, id).restitution = restitution;
        return id;
    };
    const EntityId bouncy = ball(-4.0f, 2.25f, 0.8f);
    const EntityId dead   = ball(0.0f, 2.25f, 0.0f);
    const EntityId onPad  = ball(8.0f, 2.35f, 0.0f);
    const EntityId gentle = ball(4.0f, 0.27f, 1.0f);  // lands at well under a meter a second

    // The highest each climbs once it has struck what is under it.
    struct Ball { EntityId id; float rest; bool landed = false; float peak = 0.0f; };
    std::vector<Ball> balls = {{bouncy, 0.25f}, {dead, 0.25f}, {onPad, 0.35f}, {gentle, 0.25f}};
    game.run(90, [&](int) {
        for (Ball& b : balls) {
            const float y = positionOf(scene, b.id).y;
            if (y < b.rest + 0.005f) b.landed = true;
            if (b.landed) b.peak = std::max(b.peak, y - b.rest);
        }
    });

    // 2 m of fall at 0.8 comes back about 1.28 m.
    CHECK(balls[0].peak > 1.0f);
    CHECK(balls[1].peak < 0.01f);
    CHECK(balls[2].peak > 1.0f);
    CHECK(balls[3].peak < 0.005f);
    CHECK_NEAR(positionOf(scene, gentle).y, 0.25f, 0.01f);
}

TEST("friction stops a slide, and a body with none slides on") {
    Game game;
    const EntityId floor = addFloor(game.scene());
    const EntityId rough = addCrate(game.scene(), {-10.0f, 0.5f, -3.0f});
    const EntityId slick = addCrate(game.scene(), {-10.0f, 0.5f, 3.0f});
    bodyOf(game.scene(), rough).linearVelocity = {5.0f, 0.0f, 0.0f};
    bodyOf(game.scene(), slick).linearVelocity = {5.0f, 0.0f, 0.0f};
    bodyOf(game.scene(), slick).friction       = 0.0f;
    bodyOf(game.scene(), slick).linearDamping  = 0.0f;
    bodyOf(game.scene(), floor).friction       = 0.5f;

    game.run(120);

    // At a friction of 0.5 against 0.5, 5 m/s stops in v^2 / (2 mu g) = 2.5 m.
    CHECK(glm::length(bodyOf(game.scene(), rough).linearVelocity) < 0.01f);
    CHECK_NEAR(positionOf(game.scene(), rough).x, -7.45f, 0.4f);
    CHECK(bodyOf(game.scene(), slick).linearVelocity.x > 4.9f);
    CHECK(positionOf(game.scene(), slick).x > -0.2f);
}

TEST("friction holds a box on a gentle slope and lets it slide down a steep one") {
    Game game;
    Scene& scene = game.scene();
    // Two ramps, each a static slab tipped about Z; a friction of 0.5 holds up to atan(0.5) = 26.6 degrees.
    auto ramp = [&](float z, float degrees) {
        const glm::quat tip = glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 0.0f, 1.0f));
        addBody(scene, {0.0f, 0.0f, z}, box({6.0f, 0.25f, 1.5f}), RigidbodyMotion::Static, tip);
        const EntityId crate = addBody(
            scene,
            glm::vec3(0.0f, 0.0f, z) + tip * glm::vec3(0.0f, 0.75f, 0.0f),
            box(glm::vec3(0.5f)),
            RigidbodyMotion::Dynamic,
            tip
        );
        bodyOf(scene, crate).canSleep = false;
        return crate;
    };
    const EntityId held = ramp(-3.0f, 15.0f);
    const EntityId slid = ramp(3.0f, 40.0f);
    const glm::vec3 heldStart = positionOf(scene, held), slidStart = positionOf(scene, slid);

    game.run(60);

    CHECK(glm::length(positionOf(scene, held) - heldStart) < 0.02f);
    CHECK(glm::length(positionOf(scene, slid) - slidStart) > 1.0f);
    CHECK(positionOf(scene, slid).y < slidStart.y);
}

TEST("a kinematic body goes where it is put and pushes what is in its way, never pushed back") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    const EntityId pusher = addBody(
        scene,
        {-3.0f, 0.5f, 0.0f},
        box(glm::vec3(0.5f)),
        RigidbodyMotion::Kinematic
    );
    const EntityId crate = addCrate(scene, {0.0f, 0.5f, 0.0f});
    bodyOf(scene, crate).mass = 50.0f;

    // Moved as gameplay moves one: its pose each tick, and the velocity a contact reads.
    const float speed = 2.0f;
    bool onPath = true;
    game.run(150, [&](int frame) {
        Transform& at = scene.get<Transform>(pusher);
        onPath        = onPath && std::fabs(at.position.x - (-3.0f + speed * (frame - 1) / 60.0f)) < 1e-4f;
        at.position.x = -3.0f + speed * frame / 60.0f;
        bodyOf(scene, pusher).linearVelocity = {speed, 0.0f, 0.0f};
    });

    CHECK(onPath);
    CHECK_NEAR(positionOf(scene, pusher).y, 0.5f, 1e-5f);
    CHECK(positionOf(scene, crate).x > positionOf(scene, pusher).x + 0.95f);
    CHECK(positionOf(scene, crate).x > 2.0f);
}

TEST("gravity scale and damping change a fall, and the scene's gravity sets its way") {
    Game game;
    Scene& scene = game.scene();
    auto faller = [&](float x) {
        const EntityId id = addCrate(scene, {x, 100.0f, 0.0f});
        bodyOf(scene, id).linearDamping = 0.0f;
        return id;
    };
    const EntityId plain   = faller(0.0f);
    const EntityId floats  = faller(3.0f);
    const EntityId heavy   = faller(6.0f);
    const EntityId dragged = faller(9.0f);
    bodyOf(scene, floats).gravityScale   = 0.0f;
    bodyOf(scene, heavy).gravityScale    = 2.0f;
    bodyOf(scene, dragged).linearDamping = 1.0f;
    scene.physics().gravity = {0.0f, -10.0f, 0.0f};

    game.run(60);

    // One second at 10 m/s^2 falls 5 m, give or take the step.
    const float fall = 100.0f - positionOf(scene, plain).y;
    CHECK_NEAR(fall, 5.0f, 0.1f);
    CHECK(positionOf(scene, floats).y == 100.0f);
    CHECK_NEAR((100.0f - positionOf(scene, heavy).y) / fall, 2.0f, 0.01f);
    CHECK(100.0f - positionOf(scene, dragged).y < fall * 0.8f);
    CHECK_NEAR(bodyOf(scene, plain).linearVelocity, glm::vec3(0.0f, -10.0f, 0.0f), 0.01f);
}

TEST("the scene's gravity points where it says") {
    Game game;
    const EntityId crate = addCrate(game.scene(), {0.0f, 0.0f, 0.0f});
    game.scene().physics().gravity = {4.0f, 0.0f, 0.0f};

    game.run(60);

    CHECK_NEAR(positionOf(game.scene(), crate), glm::vec3(2.0f, 0.0f, 0.0f), 0.05f);
}

TEST("a box resting on a box stays flat and still") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    const EntityId lower = addCrate(scene, {0.0f, 0.5f, 0.0f});
    const EntityId upper = addCrate(scene, {0.0f, 1.5f, 0.0f});

    game.run(300);

    CHECK_NEAR(positionOf(scene, lower), glm::vec3(0.0f, 0.5f, 0.0f), 0.005f);
    CHECK_NEAR(positionOf(scene, upper), glm::vec3(0.0f, 1.5f, 0.0f), 0.01f);
    CHECK(tiltDegrees(scene.get<Transform>(upper).rotation) < 0.1f);
    CHECK(bodyOf(scene, upper).sleeping);
}

TEST("a stack of boxes stands") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    std::vector<EntityId> stack;
    for (int i = 0; i < 8; ++i) stack.push_back(addCrate(scene, {0.0f, 0.5f + i, 0.0f}));

    game.run(600);

    const glm::vec3 top = positionOf(scene, stack.back());
    CHECK(glm::length(glm::vec2(top.x, top.z)) < 0.02f);
    CHECK_NEAR(top.y, 7.5f, 0.05f);
    for (const EntityId crate : stack) CHECK(tiltDegrees(scene.get<Transform>(crate).rotation) < 1.0f);
}

TEST("solver passes a scene asks outside their range run as the nearest end of it, and are said once") {
    // Where the top of a short stack dropped onto the floor is after a second at @p passes.
    const auto topAt = [](int passes) {
        Game game;
        Scene& scene = game.scene();
        scene.physics().solverIterations = passes;
        addFloor(scene);
        EntityId top;
        for (int i = 0; i < 3; ++i) top = addCrate(scene, {0.0f, 0.6f + 1.05f * i, 0.0f});
        game.run(60);
        return positionOf(scene, top);
    };
    const int least = PhysicsSettings::MIN_SOLVER_ITERATIONS, most = PhysicsSettings::MAX_SOLVER_ITERATIONS;

    CHECK(topAt(least - 1) == topAt(least));
    CHECK(topAt(-5) == topAt(least));
    CHECK(topAt(most * 30) == topAt(most));
    CHECK(countInLog("solver passes a step is outside") == 3);
}

TEST("a heavy body rests on a light one without sinking into it") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    const EntityId light = addCrate(scene, {0.0f, 0.5f, 0.0f});
    const EntityId heavy = addCrate(scene, {0.0f, 1.5f, 0.0f});
    bodyOf(scene, light).mass = 1.0f;
    bodyOf(scene, heavy).mass = 10.0f;

    game.run(240);

    CHECK_NEAR(positionOf(scene, light).y, 0.5f, 0.01f);
    CHECK_NEAR(positionOf(scene, heavy).y, 1.5f, 0.02f);
}

TEST("a collider's size is its own: the Transform's scale changes nothing") {
    Game game;
    addFloor(game.scene());
    const EntityId crate = addCrate(game.scene(), {0.0f, 3.0f, 0.0f});
    game.scene().get<Transform>(crate).scale = glm::vec3(3.0f);

    game.run(180);

    CHECK_NEAR(positionOf(game.scene(), crate).y, 0.5f, 0.01f);
}

TEST("a capsule lying almost along a box face rests on it, and a sphere rests at its radius") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    const glm::quat lying = glm::angleAxis(glm::radians(89.95f), glm::vec3(0.0f, 0.0f, 1.0f));
    const EntityId log    = addBody(
        scene,
        {0.0f, 1.0f, 0.0f},
        capsule(0.3f, 1.0f),
        RigidbodyMotion::Dynamic,
        lying
    );
    const EntityId sphere = addBody(scene, {4.0f, 1.0f, 0.0f}, capsule(0.4f, 0.0f));

    game.run(240);

    CHECK_NEAR(positionOf(scene, log).y, 0.3f, 0.01f);
    CHECK(tiltDegrees(scene.get<Transform>(log).rotation) > 89.0f);
    CHECK_NEAR(positionOf(scene, sphere).y, 0.4f, 0.01f);
}

TEST("a static body never moves, and a frozen rotation is never turned") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    const EntityId shelf = addBody(
        scene,
        {0.0f, 2.0f, 0.0f},
        box({1.0f, 0.1f, 1.0f}),
        RigidbodyMotion::Static
    );
    // Half over the shelf's edge: free, it would tip off.
    const EntityId frozen = addCrate(scene, {1.0f, 4.0f, 3.0f});
    const EntityId free   = addCrate(scene, {1.0f, 4.0f, 0.0f});
    addBody(scene, {0.0f, 2.0f, 3.0f}, box({1.0f, 0.1f, 1.0f}), RigidbodyMotion::Static);
    bodyOf(scene, frozen).freezeRotation = true;
    const Transform shelfBefore = scene.get<Transform>(shelf);

    game.run(180);

    CHECK(scene.get<Transform>(shelf).position == shelfBefore.position);
    CHECK(scene.get<Transform>(shelf).rotation == shelfBefore.rotation);
    CHECK(scene.get<Transform>(frozen).rotation == glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    CHECK(tiltDegrees(scene.get<Transform>(free).rotation) > 30.0f);
}

TEST("a dynamic body with no mass is held still, said once, and stands as a wall") {
    Game game;
    Scene& scene = game.scene();
    const EntityId massless = addBody(scene, {0.0f, 2.0f, 0.0f}, box({1.0f, 0.5f, 1.0f}));
    bodyOf(scene, massless).mass = 0.0f;
    const EntityId crate = addCrate(scene, {0.0f, 4.0f, 0.0f});

    game.run(180);

    CHECK(positionOf(scene, massless) == glm::vec3(0.0f, 2.0f, 0.0f));
    CHECK_NEAR(positionOf(scene, crate).y, 3.0f, 0.02f);
    CHECK(countInLog("mass") == 1);
}

TEST("a body under a moved parent rests where the world puts it, its Transform kept the parent's") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    const EntityId parent = scene.createEntity();
    Transform at;
    at.position = {10.0f, 0.0f, 0.0f};
    scene.add(parent, at);
    const EntityId crate = addCrate(scene, {0.0f, 3.0f, 0.0f});
    HierarchyOperations::setParent(scene, crate, parent);

    game.run(180);

    CHECK_NEAR(positionOf(scene, crate), glm::vec3(0.0f, 0.5f, 0.0f), 0.01f);
    CHECK_NEAR(glm::vec3(scene.get<WorldTransform>(crate).model[3]), glm::vec3(10.0f, 0.5f, 0.0f), 0.01f);
}
