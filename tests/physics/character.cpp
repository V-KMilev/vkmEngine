// The character controller: a capsule that walks, climbs steps, jumps and rides what it stands on.

#include <utility>
#include <vector>

#include "ecs/component/physics/character_controller.h"

#include "harness/check.h"
#include "harness/world.h"
#include "physics/bodies.h"

using namespace Vkm::Test;

namespace {

/// A standing capsule character 1.8 m tall, its feet at @p feet.
EntityId addCharacter(Scene& scene, const glm::vec3& feet) {
    const EntityId id = addBody(scene, feet + glm::vec3(0.0f, 0.9f, 0.0f), capsule(0.3f, 0.6f));
    Rigidbody& body   = bodyOf(scene, id);
    body.mass           = 70.0f;
    body.freezeRotation = true;
    body.canSleep       = false;
    scene.add(id, CharacterController{});
    return id;
}

CharacterController& controllerOf(Scene& scene, EntityId id) { return scene.get<CharacterController>(id); }
float feetOf(Scene& scene, EntityId id) { return positionOf(scene, id).y - 0.9f; }

} // namespace

TEST("a character walks at the speed asked, stays grounded and slides along a wall") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    // A wall across the path at a slant: walking into it slides along it.
    addBody(
        scene,
        {6.0f, 1.0f, 0.0f},
        box({0.25f, 1.0f, 6.0f}),
        RigidbodyMotion::Static,
        glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f))
    );
    const EntityId walker = addCharacter(scene, {0.0f, 0.0f, 0.0f});
    controllerOf(scene, walker).moveInput = {3.0f, 0.0f, 0.0f};

    bool alwaysGrounded = true;
    float speedAt1s     = 0.0f;
    game.run(240, [&](int frame) {
        if (frame > 10) alwaysGrounded = alwaysGrounded && controllerOf(scene, walker).grounded;
        if (frame == 60) speedAt1s = controllerOf(scene, walker).walkSpeed;
    });

    CHECK(alwaysGrounded);
    CHECK_NEAR(speedAt1s, 3.0f, 0.05f);
    CHECK_NEAR(feetOf(scene, walker), 0.0f, 0.02f);
    // Held on its side of the wall, and carried along it rather than stopped.
    const glm::vec3 across = glm::angleAxis(
        glm::radians(30.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    ) * glm::vec3(1.0f, 0.0f, 0.0f);
    CHECK(glm::dot(positionOf(scene, walker) - glm::vec3(6.0f, 0.9f, 0.0f), across) < -0.5f);
    CHECK(std::fabs(positionOf(scene, walker).z) > 1.0f);
}

TEST("a character climbs a slope within its limit and not one past it") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    // A ramp 10 m long rising from the floor at x = 1.
    auto ramp = [&](float z, float degrees) {
        const float a = glm::radians(degrees);
        const glm::quat tip = glm::angleAxis(a, glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::vec3 middle(1.0f + 5.0f * std::cos(a), 5.0f * std::sin(a), z);
        addBody(
            scene,
            middle + tip * glm::vec3(0.0f, -0.25f, 0.0f),
            box({5.0f, 0.25f, 1.5f}),
            RigidbodyMotion::Static,
            tip
        );
    };
    ramp(-3.0f, 30.0f);
    ramp(3.0f, 60.0f);
    const EntityId gentle = addCharacter(scene, {0.0f, 0.0f, -3.0f});
    const EntityId steep  = addCharacter(scene, {0.0f, 0.0f, 3.0f});
    controllerOf(scene, gentle).moveInput = {2.0f, 0.0f, 0.0f};
    controllerOf(scene, steep).moveInput  = {2.0f, 0.0f, 0.0f};

    float steepHighest = 0.0f;
    game.run(180, [&](int) { steepHighest = std::max(steepHighest, feetOf(scene, steep)); });

    // Three seconds at 2 m/s up a 30 degree slope climbs well over 1.5 m.
    CHECK(feetOf(scene, gentle) > 1.5f);
    CHECK(controllerOf(scene, gentle).grounded);
    CHECK(steepHighest < 0.6f);
    CHECK(positionOf(scene, steep).x < 1.5f);
}

TEST("a character jumps when asked from the ground, and not in the air") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    const EntityId jumper = addCharacter(scene, {0.0f, 0.0f, 0.0f});

    float peak       = 0.0f;
    float secondPeak = 0.0f;
    bool jumpingSeen = false;
    game.run(150, [&](int frame) {
        CharacterController& c = controllerOf(scene, jumper);
        if (frame == 20 || frame == 30) c.jumpRequested = true;  // the second mid-air
        jumpingSeen = jumpingSeen || c.jumping;
        if (frame < 100) peak = std::max(peak, feetOf(scene, jumper));
        else secondPeak = std::max(secondPeak, feetOf(scene, jumper));
    });

    // 5 m/s up rises v^2 / 2g = 1.27 m.
    CHECK_NEAR(peak, 1.27f, 0.1f);
    CHECK(jumpingSeen);
    CHECK(secondPeak < 0.05f);
    CHECK(controllerOf(scene, jumper).grounded);
}

TEST("a character mounts a step under its step height and stops at one above") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    auto curb = [&](float z, float height) {
        addBody(scene, {5.0f, height / 2.0f, z}, box({2.0f, height / 2.0f, 1.0f}), RigidbodyMotion::Static);
    };
    curb(-3.0f, 0.3f);
    curb(3.0f, 0.6f);
    const EntityId low  = addCharacter(scene, {0.0f, 0.0f, -3.0f});
    const EntityId high = addCharacter(scene, {0.0f, 0.0f, 3.0f});
    controllerOf(scene, low).moveInput  = {2.0f, 0.0f, 0.0f};
    controllerOf(scene, high).moveInput = {2.0f, 0.0f, 0.0f};

    game.run(150);

    CHECK_NEAR(feetOf(scene, low), 0.3f, 0.03f);
    CHECK(positionOf(scene, low).x > 4.0f);
    CHECK_NEAR(feetOf(scene, high), 0.0f, 0.03f);
    CHECK(positionOf(scene, high).x < 3.0f);
}

TEST("a character rides a moving platform, and turns with a turning one") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene, -5.0f);
    const EntityId deck = addBody(
        scene,
        {0.0f, -0.25f, 0.0f},
        box({3.0f, 0.25f, 3.0f}),
        RigidbodyMotion::Kinematic
    );
    const EntityId wheel = addBody(
        scene,
        {20.0f, -0.25f, 0.0f},
        box({4.0f, 0.25f, 4.0f}),
        RigidbodyMotion::Kinematic
    );
    const EntityId rider = addCharacter(scene, {0.0f, 0.0f, 0.0f});
    const EntityId spun  = addCharacter(scene, {22.0f, 0.0f, 0.0f});

    const float speed = 1.5f, spin = 0.5f;
    game.run(120, [&](int frame) {
        const float t = frame / 60.0f;
        scene.get<Transform>(deck).position.x   = speed * t;
        bodyOf(scene, deck).linearVelocity      = {speed, 0.0f, 0.0f};
        scene.get<Transform>(wheel).rotation    = glm::angleAxis(spin * t, glm::vec3(0.0f, 1.0f, 0.0f));
        bodyOf(scene, wheel).angularVelocity    = {0.0f, spin, 0.0f};
    });

    // Two seconds on the deck carried it 3 m; on the wheel, a radian round from 2 m out.
    CHECK_NEAR(positionOf(scene, rider).x, 3.0f, 0.1f);
    CHECK(controllerOf(scene, rider).grounded);
    CHECK_NEAR(controllerOf(scene, rider).groundVelocity, glm::vec3(speed, 0.0f, 0.0f), 0.05f);
    const glm::vec3 round = positionOf(scene, spun) - glm::vec3(20.0f, 0.0f, 0.0f);
    CHECK_NEAR(glm::length(glm::vec2(round.x, round.z)), 2.0f, 0.1f);
    CHECK_NEAR(std::atan2(-round.z, round.x), 1.0f, 0.1f);
    CHECK_NEAR(controllerOf(scene, spun).groundSpin, spin, 0.01f);
}

TEST("a crouching character shortens with its feet in place and stands once there is room") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    // A ceiling 1.4 m up over x > 2.
    addBody(scene, {5.0f, 1.65f, 0.0f}, box({3.0f, 0.25f, 2.0f}), RigidbodyMotion::Static);
    const EntityId crouched = addCharacter(scene, {0.0f, 0.0f, 0.0f});
    controllerOf(scene, crouched).crouchHeight = 1.0f;

    bool shortUnder = false, staysShort = false;
    game.run(300, [&](int frame) {
        CharacterController& c = controllerOf(scene, crouched);
        if (frame == 10) {
            c.crouching = true;
            c.moveInput = {2.0f, 0.0f, 0.0f};
        }
        if (frame == 100) {
            shortUnder  = c.crouched && scene.get<Collider>(crouched).parts[0].halfHeight < 0.3f;
            c.crouching = false;
            c.moveInput = {0.0f, 0.0f, 0.0f};
        }
        if (frame == 150) {
            staysShort  = c.crouched;
            c.moveInput = {-3.0f, 0.0f, 0.0f};
        }
    });

    CHECK(shortUnder);
    CHECK(staysShort);
    CHECK(!controllerOf(scene, crouched).crouched);
    CHECK_NEAR(scene.get<Collider>(crouched).parts[0].halfHeight, 0.6f, 1e-4f);
    CHECK(std::fabs(feetOf(scene, crouched)) < 0.03f);
}

TEST("characters are stepped in slot order, so one riding another moves alike whatever order they were made in") {
    // One character on another's head, the lower walking off: the rider takes the walker's velocity, so
    // which of the two is stepped first is part of the answer.
    const auto ride = [](bool riderFirst) {
        Game game;
        Scene& scene = game.scene();
        addFloor(scene);
        const EntityId walker = scene.createEntity();
        const EntityId rider  = scene.createEntity();
        // The components added in either order, so the stores hold the two in either order; the slots
        // are the same.
        std::vector<EntityId> order = {walker, rider};
        if (riderFirst) std::swap(order[0], order[1]);
        for (const EntityId id : order) {
            const float feet = id == rider ? 1.8f : 0.0f;
            Transform at;
            at.position = {0.0f, feet + 0.9f, 0.0f};
            scene.add(id, at);
            Rigidbody body;
            body.mass           = 70.0f;
            body.freezeRotation = true;
            body.canSleep       = false;
            scene.add(id, body);
            Collider collider;
            collider.parts = {capsule(0.3f, 0.6f)};
            scene.add(id, collider);
            scene.add(id, CharacterController{});
        }
        game.run(40, [&](int frame) {
            if (frame == 10) controllerOf(scene, walker).moveInput = {2.0f, 0.0f, 0.0f};
        });
        return positionOf(scene, rider);
    };

    const glm::vec3 walkerFirst = ride(false);
    CHECK(walkerFirst.y > 2.0f);
    CHECK(ride(true) == walkerFirst);
}
