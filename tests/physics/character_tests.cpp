#include "physics/physics_support.h"

#include <cmath>
#include <limits>

#include "ecs/component/physics/character_controller.h"
#include "ecs/component/render/terrain.h"
#include "system/physics/character/character_controller_system.h"
#include "system/physics/physics_system.h"

namespace {

// A staircase is a kerb's step taken eight times without touching ground between.
// The climb ends the tick after it begins, so mounting one tread must not bounce
// off the next.
void testCharacterStaircase() {
    std::printf("Character staircase:\n");

    constexpr float RADIUS = 0.3f, HALF = 0.61f;
    constexpr float RISER = 0.26f, TREAD = 0.85f;
    constexpr int   STEPS = 7;

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {6.0f, 0.5f, 4.0f});
    for (int i = 0; i < STEPS; ++i) {
        const float h = RISER * static_cast<float>(i + 1);
        addBox(scene, {2.0f + static_cast<float>(i) * TREAD, h * 0.5f, 0.0f}, {TREAD * 0.5f, h * 0.5f, 2.0f});
    }
    // A landing, so the run measures the climb and not the fall off the end.
    const float top = RISER * static_cast<float>(STEPS);
    addBox(scene, {2.0f + STEPS * TREAD + 1.5f, top * 0.5f, 0.0f}, {2.0f, top * 0.5f, 2.0f});
    const EntityId walker = addCharacter(scene, RADIUS, HALF, 0.4f);

    float highest = 0.0f;
    for (int i = 0; i < 30; ++i) {
        simulate(scene, 10);
        highest = glm::max(highest, scene.get<Transform>(walker).position.y);
    }

    const glm::vec3 end = scene.get<Transform>(walker).position;
    check("a character climbs a whole staircase", end.y > (HALF + RADIUS) + top - RISER);
    check("  arriving at the top of it", end.x > 2.0f + (STEPS - 1) * TREAD);
    // Mounted, not hopped: a jump from the bottom would clear more than a metre,
    // one from any tread would overshoot the next.
    check("  a tread at a time, without jumping any of them", highest < (HALF + RADIUS) + top + RISER);
}

// A character that stops at a kerb cannot use stairs.
void testCharacterStepUp() {
    std::printf("Character step-up:\n");

    constexpr float RADIUS = 0.3f;
    constexpr float HALF   = 0.5f;
    constexpr float KERB   = 0.3f;

    // A floor, and a kerb 2m along whose top is at 0.3.
    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    addBox(scene, {3.0f, KERB * 0.5f, 0.0f}, {1.0f, KERB * 0.5f, 4.0f});
    const EntityId walker = addCharacter(scene, RADIUS, HALF, 0.4f);

    // Long enough to walk along the kerb's top but short of its far edge, where the
    // character would step back down.
    simulate(scene, 120);
    const glm::vec3 stepped = scene.get<Transform>(walker).position;

    check("a character mounts a kerb below its step height", stepped.y > (HALF + RADIUS) + KERB * 0.5f);
    check("  standing on top of it, not wedged against it", stepped.x > 3.0f);

    simulate(scene, 120);
    const glm::vec3 descended = scene.get<Transform>(walker).position;
    check(
        "  and steps back down off the far edge",
        descended.x > 4.5f && descended.y < (HALF + RADIUS) + KERB * 0.5f
    );

    // Every assertion above passes for a hop too: a climb asking for stepHeight at
    // walking speed launches off a doorstep and still ends standing on the kerb.
    Scene hop;
    addBox(hop, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    addBox(hop, {3.0f, KERB * 0.5f, 0.0f}, {1.0f, KERB * 0.5f, 4.0f});
    const EntityId mounting = addCharacter(hop, RADIUS, HALF, 0.4f);

    float highest = 0.0f;
    for (int step = 0; step < 24; ++step) {
        simulate(hop, 10);
        highest = glm::max(highest, hop.get<Transform>(mounting).position.y);
    }
    // On the kerb the centre is at 1.1; much above was airborne (a jump reaches ~2.5).
    check("  without ever leaving the ground to do it", highest < 1.35f);

    // Step-up switched off: the kerb is a wall again.
    Scene blocked;
    addBox(blocked, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    addBox(blocked, {3.0f, KERB * 0.5f, 0.0f}, {1.0f, KERB * 0.5f, 4.0f});
    const EntityId stopped = addCharacter(blocked, RADIUS, HALF, 0.0f);

    simulate(blocked, 120);
    const glm::vec3 held = blocked.get<Transform>(stopped).position;

    check("stepHeight of zero leaves the kerb a wall", held.x < 2.5f);

    // A step taller than the limit is refused, or the character climbs out of the level.
    Scene tall;
    addBox(tall, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    addBox(tall, {3.0f, 0.6f, 0.0f}, {1.0f, 0.6f, 4.0f});
    const EntityId refused = addCharacter(tall, RADIUS, HALF, 0.4f);

    simulate(tall, 120);
    check("a step above the limit is refused", tall.get<Transform>(refused).position.x < 2.5f);
}

// A jump pressed while mounting a step. The climb sets vertical speed outright each
// tick it runs, so a jump that left it running would be overwritten and rise only a
// kerb's height.
void testAJumpDuringAStepIsAWholeJump() {
    std::printf("A jump pressed while mounting a step:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    addBox(scene, {3.0f, 0.15f, 0.0f}, {1.0f, 0.15f, 4.0f});
    const EntityId walker = addCharacter(scene, 0.3f, 0.5f, 0.4f);
    CharacterController& controller = scene.get<CharacterController>(walker);

    TestFrame frame(scene);
    PhysicsSystem physics;
    CharacterControllerSystem controllers;

    bool jumped = false;
    float highest = 0.0f;
    for (int tick = 0; tick < 150; ++tick) {
        // On the tick the climb begins, still grounded: the one moment both are true.
        if (!jumped && controller.stepping && controller.grounded) {
            controller.jumpRequested = true;
            jumped = true;
        }
        physics.fixedUpdate(frame.ctx);
        controllers.fixedUpdate(frame.ctx);
        if (jumped) highest = glm::max(highest, scene.get<Transform>(walker).position.y);
    }

    // On the floor the centre is at 0.8; a 5 m/s jump lifts it about 1.25 m.
    check("the climb began and the jump was asked for", jumped);
    check("  and the character rose a jump's height, not a kerb's", highest > 1.6f);
}

// A carried ragdoll's kinematic bones ride the legs on a layer the character
// ignores. The step probes must see only what the body collides with: a clearance
// sweep hitting its own shin would call every staircase blocked.
void testStepUpPastOwnBones() {
    std::printf("Step-up past a body on an ignored layer:\n");

    constexpr float RADIUS = 0.3f;
    constexpr float HALF   = 0.5f;
    constexpr float KERB   = 0.3f;

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    addBox(scene, {3.0f, KERB * 0.5f, 0.0f}, {1.0f, KERB * 0.5f, 4.0f});
    const EntityId walker = addCharacter(scene, RADIUS, HALF, 0.4f);
    scene.get<Rigidbody>(walker).collidesWith = ~2;

    // A shin: a kinematic capsule exactly where the probes look.
    const EntityId shin = scene.createEntity();
    Transform t;
    t.position = {0.35f, 0.4f, 0.0f};
    scene.add<Transform>(shin, std::move(t));
    Rigidbody rb;
    rb.motion = RigidbodyMotion::Kinematic;
    rb.layer = 2;
    scene.add<Rigidbody>(shin, std::move(rb));
    ColliderPart part;
    part.shape = ColliderShape::Capsule;
    part.radius = 0.06f;
    part.halfHeight = 0.2f;
    Collider c;
    c.parts = { part };
    scene.add<Collider>(shin, std::move(c));

    // Carried by hand each tick, as RagdollSystem carries bones.
    for (int i = 0; i < 12; ++i) {
        simulate(scene, 10);
        scene.get<Transform>(shin).position =
            scene.get<Transform>(walker).position + glm::vec3(0.35f, -0.6f, 0.0f);
    }

    const glm::vec3 end = scene.get<Transform>(walker).position;
    check(
        "a character steps up with a bone riding its leg",
        end.y > (HALF + RADIUS) + KERB * 0.5f && end.x > 3.0f
    );
}

// How far walking into a crate for two seconds carries it. Contact impulse is shared
// by mass, so a hundred-times-heavier crate must move far less; a per-tick steer
// that ignored what it pushed would push anything alike.
float carriedBy(float crateMass) {
    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {20.0f, 0.5f, 4.0f});
    addCharacter(scene, 0.3f, 0.5f, 0.0f);

    const EntityId crate = scene.createEntity();
    Transform transform;
    transform.position = {1.5f, 0.5f, 0.0f};
    scene.add<Transform>(crate, std::move(transform));
    Rigidbody body;
    body.mass = crateMass;
    body.freezeRotation = true;
    scene.add<Rigidbody>(crate, std::move(body));
    ColliderPart part;
    part.shape = ColliderShape::Box;
    part.halfExtents = {0.5f, 0.5f, 0.5f};
    Collider collider;
    collider.parts = { part };
    scene.add<Collider>(crate, std::move(collider));

    simulate(scene, 128);
    return scene.get<Transform>(crate).position.x - 1.5f;
}

// How far a character standing still slides from a shove of 10 m/s, with @p drag.
float slideFromAShove(float drag) {
    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {40.0f, 0.5f, 4.0f});
    const EntityId walker = addCharacter(scene, 0.3f, 0.5f, 0.0f);
    CharacterController& controller = scene.get<CharacterController>(walker);
    controller.moveInput     = glm::vec3(0.0f);
    controller.knockbackDrag = drag;
    simulate(scene, 30);  // settled on the floor
    scene.get<Rigidbody>(walker).linearVelocity = {10.0f, 0.0f, 0.0f};
    const float from = scene.get<Transform>(walker).position.x;
    simulate(scene, 180);
    return scene.get<Transform>(walker).position.x - from;
}

// A shove is speed nobody asked for: without knockbackDrag the controller steers it
// away at its full acceleration; with it, the shove carries and dies at the drag.
void testAShoveCarriesWithKnockbackDrag() {
    std::printf("A shove, with and without knockbackDrag:\n");
    const float stopped = slideFromAShove(0.0f);
    const float carried = slideFromAShove(10.0f);
    std::printf("      no drag slid %.3f m, drag 10 slid %.3f m\n", stopped, carried);
    // At 40 m/s^2 a 10 m/s shove is gone in a quarter second, 1.25 m, and floor
    // friction takes a little more. At the drag plus the airborne share of steering,
    // 10 and 10, it lasts half a second, 2.5 m, less the same friction: about 2.
    check("without knockbackDrag a shove dies at the acceleration", stopped < 1.4f);
    check("  and with it the shove carries", carried > 1.7f && carried < 2.6f);
}

// How far a character walking at 4 m/s slides once its input lets go, with @p drag.
float slideFromAStop(float drag) {
    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {40.0f, 0.5f, 4.0f});
    const EntityId walker = addCharacter(scene, 0.3f, 0.5f, 0.0f);
    CharacterController& controller = scene.get<CharacterController>(walker);
    controller.knockbackDrag = drag;
    controller.moveInput     = {4.0f, 0.0f, 0.0f};
    simulate(scene, 90);  // settled, and up to speed
    controller.moveInput = glm::vec3(0.0f);
    const float from = scene.get<Transform>(walker).position.x;
    simulate(scene, 120);
    return scene.get<Transform>(walker).position.x - from;
}

// Letting go of the stick is not a shove: a walker stops at its full acceleration
// whatever its knockbackDrag.
void testLettingGoIsNotAShove() {
    std::printf("A walker letting go, with and without knockbackDrag:\n");
    const float plain = slideFromAStop(0.0f);
    const float drag  = slideFromAStop(10.0f);
    std::printf("      no drag slid %.3f m, drag 10 slid %.3f m\n", plain, drag);
    check("it stops as soon with a knockbackDrag as without", std::abs(drag - plain) < 0.05f);
}

// A character's body made kinematic is placed by script - a cut-scene, a seat, a pause -
// and the controller leaves it be: no velocity written, nothing to warn about.
void testAKinematicCharacterIsLeftAlone() {
    std::printf("A character whose body is kinematic:\n");
    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    const EntityId walker = addCharacter(scene, 0.3f, 0.5f, 0.0f);
    Rigidbody& body = scene.get<Rigidbody>(walker);
    body.motion = RigidbodyMotion::Kinematic;
    scene.get<Collider>(walker).enabled = false;
    const glm::vec3 from = scene.get<Transform>(walker).position;
    simulate(scene, 64);
    const float speed = glm::length(scene.get<Rigidbody>(walker).linearVelocity);
    check("the controller writes it no velocity", speed == 0.0f);
    check("  and it stays where the script put it", scene.get<Transform>(walker).position == from);

    // A jump pressed while it was placed is spent then, not fired once it moves again.
    scene.get<CharacterController>(walker).jumpRequested = true;
    simulate(scene, 1);
    check("  a jump pressed meanwhile is spent", !scene.get<CharacterController>(walker).jumpRequested);
    body.motion = RigidbodyMotion::Dynamic;
    scene.get<Collider>(walker).enabled = true;
    simulate(scene, 30);
    const float rose = scene.get<Transform>(walker).position.y - from.y;
    check("  and moving again it does not jump", rose < 0.05f);
}

// A character that may sleep dozes off standing still; the velocity a request to walk gives it wakes
// it on the next tick's gather, before writeback would zero it.
void testADozingCharacterWalksWhenAsked() {
    std::printf("A character that dozed off, asked to walk:\n");
    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    const EntityId walker = addCharacter(scene, 0.3f, 0.5f, 0.0f);
    scene.get<Rigidbody>(walker).canSleep = true;
    scene.get<CharacterController>(walker).moveInput = glm::vec3(0.0f);
    bool dozed = false;
    for (int i = 0; i < 60 && !dozed; ++i) {
        simulate(scene, 10);
        dozed = scene.get<Rigidbody>(walker).sleeping;
    }
    check("standing still, it dozes off", dozed);

    const float from = scene.get<Transform>(walker).position.x;
    scene.get<CharacterController>(walker).moveInput = {2.0f, 0.0f, 0.0f};
    simulate(scene, 60);
    check("  asked to walk, it walks", scene.get<Transform>(walker).position.x - from > 0.5f);
}

// A character asks for room as collision would: a beam that does not collide with its layer is no
// ceiling to it, though its own mask takes the beam's layer.
void testACrouchStandsUnderWhatDoesNotCollideWithIt() {
    std::printf("A crouched character under a beam that does not collide with it:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {10.0f, 0.5f, 4.0f});
    const EntityId beam   = addBox(scene, {0.0f, 1.4f, 0.0f}, {0.5f, 0.2f, 4.0f});
    const EntityId walker = addCharacter(scene, 0.3f, 0.6f, 0.4f);
    scene.get<Rigidbody>(beam).collidesWith = 1;
    scene.get<Rigidbody>(walker).layer      = 2;
    CharacterController& cc = scene.get<CharacterController>(walker);
    cc.crouchHeight = 1.0f;
    cc.crouched     = true;
    cc.moveInput    = glm::vec3(0.0f);
    TestFrame frame(scene);
    CharacterControllerSystem controller;
    controller.fixedUpdate(frame.ctx);
    check("it stands", !cc.crouched);
}

// A beam at head height stops a character standing and lets one crouching under; let go beneath
// it, the character stays crouched until it has walked out from under.
void testACrouchPassesUnderABeamAndStandsOnlyWithRoom() {
    std::printf("A character crouching under a beam:\n");

    constexpr float RADIUS = 0.3f, HALF = 0.6f;
    const auto world = [&](Scene& scene) {
        addBox(scene, {0.0f, -0.5f, 0.0f}, {10.0f, 0.5f, 4.0f});
        addBox(scene, {2.5f, 1.4f, 0.0f}, {0.5f, 0.2f, 4.0f});
        const EntityId walker = addCharacter(scene, RADIUS, HALF, 0.4f);
        scene.get<CharacterController>(walker).crouchHeight = 1.0f;
        return walker;
    };

    Scene upright;
    const EntityId standing = world(upright);
    simulate(upright, 150);
    check("standing, the beam stops it", upright.get<Transform>(standing).position.x < 2.0f);

    Scene low;
    const EntityId walker = world(low);
    CharacterController& cc = low.get<CharacterController>(walker);
    const auto part = [&]() { return low.get<Collider>(walker).parts[0]; };
    cc.crouching = true;
    simulate(low, 1);
    const float feet = low.get<Transform>(walker).position.y + part().center.y - part().halfHeight - RADIUS;
    const float tall = 2.0f * (part().halfHeight + RADIUS);
    check("crouched, the capsule is crouchHeight tall", std::abs(tall - 1.0f) < 1e-5f);
    check("  its feet where they were", std::abs(feet) < 0.01f);

    simulate(low, 74);
    const float under = low.get<Transform>(walker).position.x;
    check("  and it walks under the beam", under > 2.0f && under < 3.0f);
    cc.crouching = false;
    cc.moveInput = glm::vec3(0.0f);
    simulate(low, 20);
    check("let go beneath it, it stays crouched", cc.crouched && part().halfHeight < HALF);
    cc.moveInput = {2.0f, 0.0f, 0.0f};
    simulate(low, 100);
    check("walked out, it stands", !cc.crouched && part().halfHeight == HALF && part().center.y == 0.0f);

    // Told it is crouched, as a snapshot tells a client, while its capsule still stands: it still
    // asks for room before standing, rather than taking the tall capsule for an answer.
    Scene told;
    const EntityId crouched = world(told);
    told.get<Transform>(crouched).position.x = 2.5f;
    CharacterController& snapshot = told.get<CharacterController>(crouched);
    snapshot.crouched  = true;
    snapshot.moveInput = glm::vec3(0.0f);
    TestFrame frame(told);
    CharacterControllerSystem controller;
    controller.fixedUpdate(frame.ctx);
    const ColliderPart told0 = told.get<Collider>(crouched).parts[0];
    check("told it is crouched under the beam, it stays so", snapshot.crouched && told0.halfHeight < HALF);
}

// A deck that slides, and one that turns, carry what stands on them: let go, a character stays
// where it stood on the deck, and on a turntable goes round with it, drifting out only as the
// step along the tangent takes anything on it (physics.md, "Riding what it stands on").
void testACharacterRidesWhatItStandsOn() {
    std::printf("A character on a moving deck:\n");

    const auto deck = [](Scene& scene, glm::vec3 velocity, glm::vec3 spin) {
        const EntityId id = addBox(scene, {0.0f, -0.5f, 0.0f}, {6.0f, 0.5f, 6.0f});
        Rigidbody& body     = scene.get<Rigidbody>(id);
        body.motion          = RigidbodyMotion::Kinematic;
        body.linearVelocity  = velocity;
        body.angularVelocity = spin;
        return id;
    };
    // The deck moved as a driver moves one: its Transform, at the velocities it carries.
    const auto drive = [](Scene& scene, EntityId id) {
        constexpr float DT = 1.0f / static_cast<float>(Config::DEFAULT_TICK_RATE);
        const Rigidbody& body = scene.get<Rigidbody>(id);
        Transform&       at   = scene.get<Transform>(id);
        at.position += body.linearVelocity * DT;
        const float turn = glm::length(body.angularVelocity) * DT;
        if (turn > 0.0f) {
            at.rotation = glm::angleAxis(turn, glm::normalize(body.angularVelocity)) * at.rotation;
        }
    };

    Scene sliding;
    const EntityId slider = deck(sliding, {1.5f, 0.0f, 0.0f}, glm::vec3(0.0f));
    const EntityId rider  = addCharacter(sliding, 0.3f, 0.5f, 0.4f);
    sliding.get<CharacterController>(rider).moveInput = glm::vec3(0.0f);
    simulate(sliding, 128, [&]() { drive(sliding, slider); });
    const float apart = sliding.get<Transform>(rider).position.x - sliding.get<Transform>(slider).position.x;
    std::printf("      after 2 s on a deck at 1.5 m/s, %.3f m from where it stood on it\n", apart);
    check("standing still on a sliding deck, it goes with it", std::abs(apart) < 0.05f);

    // One that carries shoves (knockbackDrag) does not take the deck's speed under it for one: it
    // lags its deck no more than one that carries none.
    const auto lagOn3 = [&](float drag) {
        Scene scene;
        const EntityId fast   = deck(scene, {3.0f, 0.0f, 0.0f}, glm::vec3(0.0f));
        const EntityId braced = addCharacter(scene, 0.3f, 0.5f, 0.4f);
        scene.get<CharacterController>(braced).moveInput     = glm::vec3(0.0f);
        scene.get<CharacterController>(braced).knockbackDrag = drag;
        simulate(scene, 128, [&]() { drive(scene, fast); });
        return scene.get<Transform>(braced).position.x - scene.get<Transform>(fast).position.x;
    };
    const float plain   = lagOn3(0.0f);
    const float dragged = lagOn3(4.0f);
    std::printf("      on a 3 m/s deck, %.3f m behind, %.3f m with knockbackDrag\n", plain, dragged);
    check("  even one that carries shoves", std::abs(dragged - plain) < 0.01f);
    const float carry = sliding.get<CharacterController>(rider).groundVelocity.x;
    check("  and says how fast", std::abs(carry - 1.5f) < 1e-3f);

    // The rider made first here, so it is the other body of its pair than on the sliding deck.
    Scene turning;
    const EntityId spun  = addCharacter(turning, 0.3f, 0.5f, 0.4f);
    const EntityId table = deck(turning, glm::vec3(0.0f), {0.0f, 1.0f, 0.0f});
    turning.get<CharacterController>(spun).moveInput = glm::vec3(0.0f);
    turning.get<Transform>(spun).position.x = 3.0f;
    simulate(turning, 192, [&]() { drive(turning, table); });
    const glm::vec3 at = turning.get<Transform>(spun).position;
    const float radius = glm::length(glm::vec2(at.x, at.z));
    const float round  = std::atan2(-at.z, at.x);
    std::printf("      after 3 s at 1 rad/s, %.3f m from the middle, %.2f rad round\n", radius, round);
    check("on a turntable it keeps its distance from the middle, but drifts", std::abs(radius - 3.0f) < 0.1f);
    check("  going round with it", std::abs(round - 3.0f) < 0.1f);
    check("  and says how fast it turns", turning.get<CharacterController>(spun).groundSpin == 1.0f);
}

// A dynamic body's velocity is its centre of mass's, so what stands on one pivoted off its middle
// is carried at the speed of the surface under it, not of a point turning about the pivot.
void testARiderTakesTheSpeedOfTheSurfaceUnderIt() {
    std::printf("A character on a spinning crate pivoted off its middle:\n");
    Scene scene;
    addBox(scene, {0.0f, -1.5f, 0.0f}, {10.0f, 0.5f, 10.0f});
    const EntityId crate = scene.createEntity();
    Transform pivot;
    pivot.position = {-2.0f, -1.0f, 0.0f};
    scene.add<Transform>(crate, std::move(pivot));
    Rigidbody body;
    body.mass = 5000.0f;
    scene.add<Rigidbody>(crate, std::move(body));
    ColliderPart part;
    part.shape       = ColliderShape::Box;
    part.center      = {2.0f, 0.5f, 0.0f};
    part.halfExtents = {3.0f, 0.5f, 3.0f};
    Collider collider;
    collider.parts = {part};
    scene.add<Collider>(crate, std::move(collider));

    const EntityId rider = addCharacter(scene, 0.3f, 0.5f, 0.4f);
    scene.get<CharacterController>(rider).moveInput = glm::vec3(0.0f);
    scene.get<Transform>(rider).position.x = 1.0f;
    simulate(scene, 10, [&]() {
        scene.get<Rigidbody>(crate).linearVelocity  = glm::vec3(0.0f);
        scene.get<Rigidbody>(crate).angularVelocity = {0.0f, 1.0f, 0.0f};
    });

    const Rigidbody& spun   = scene.get<Rigidbody>(crate);
    const Transform& at     = scene.get<Transform>(crate);
    const glm::vec3  centre = at.position + at.rotation * part.center;
    const glm::vec3  feet   = scene.get<Transform>(rider).position - glm::vec3(0.0f, 0.8f, 0.0f);
    const glm::vec3  under  = spun.linearVelocity + glm::cross(spun.angularVelocity, feet - centre);
    const glm::vec3  told   = scene.get<CharacterController>(rider).groundVelocity;
    std::printf("      told %.3f m/s, the surface moves %.3f m/s\n", glm::length(told), glm::length(under));
    check("it is carried at the speed of the surface under its feet", glm::length(told - under) < 0.05f);
}

// A ladder is a wall gameplay says to climb, each tick it holds on: pushing into it goes up,
// letting go hangs, a jump leaps off, and once gameplay stops saying so it falls.
void testACharacterClimbsAWall() {
    std::printf("A character climbing a wall:\n");
    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    addBox(scene, {1.5f, 4.0f, 0.0f}, {0.5f, 4.0f, 4.0f});  // its face at x = 1
    const EntityId climber = addCharacter(scene, 0.3f, 0.5f, 0.4f);
    simulate(scene, 30);  // walked up against it
    CharacterController& cc = scene.get<CharacterController>(climber);
    cc.climbNormal = {-1.0f, 0.0f, 0.0f};
    cc.moveInput   = {5.0f, 0.0f, 0.0f};  // faster than it climbs
    const auto holdOn = [&cc] { cc.climbing = true; };
    const float foot = scene.get<Transform>(climber).position.y;
    simulate(scene, 64, holdOn);
    const float climbed = scene.get<Transform>(climber).position.y - foot;
    std::printf("      a second up it climbed %.2f m\n", static_cast<double>(climbed));
    check("pushing into it climbs at about its climbSpeed", climbed > 2.5f && climbed < 3.1f);

    const glm::vec3& at = scene.get<Transform>(climber).position;
    cc.moveInput = glm::vec3(0.0f);
    simulate(scene, 16, holdOn);  // coming to a stop
    const float held = at.y;
    simulate(scene, 64, holdOn);
    std::printf("      let go, it moved %.3f m\n", static_cast<double>(at.y - held));
    check("  letting go of the input, it hangs there", std::abs(at.y - held) < 0.05f);

    cc.moveInput = {0.0f, 0.0f, 2.0f};
    const float side = at.z;
    simulate(scene, 32, holdOn);
    check(
        "  pushing along it, it edges sideways, still up",
        at.z - side > 0.3f && std::abs(at.y - held) < 0.1f
    );

    cc.moveInput     = glm::vec3(0.0f);
    cc.jumpRequested = true;
    const float face = at.x;
    simulate(scene, 1, holdOn);
    check("  a jump leaps up off it", scene.get<Rigidbody>(climber).linearVelocity.y > 0.5f * cc.jumpSpeed);
    simulate(scene, 19);
    std::printf("      it leapt %.3f m off\n", static_cast<double>(face - at.x));
    check("  leaping off, away from the face", at.x < face - 0.2f);

    // Held on again, high up, then no longer: nothing keeps it there.
    simulate(scene, 64, holdOn);
    const float hung = at.y;
    simulate(scene, 32);
    std::printf("      no longer held, it fell %.2f m\n", static_cast<double>(hung - at.y));
    check("  once gameplay stops holding it on, it falls", hung - at.y > 0.5f);
}

void testAHeavierCrateIsHarderToPush() {
    std::printf("A character pushing crates of different mass:\n");
    const float light = carriedBy(8.0f);
    const float heavy = carriedBy(800.0f);
    std::printf("      8 kg carried %.3f m, 800 kg carried %.3f m\n", light, heavy);
    check("the light crate is pushed", light > 0.5f);
    check("  and the heavy one far less", heavy < light * 0.25f);
}

// Ground whose height runs along X alone, walked along +X: a floor of slopes and crests.
using Profile = float (*)(float);

// A static mesh following @p height from @p from to @p to along X, a strip across Z.
MeshAsset makeProfileMesh(Profile height, float from, float to, int cells) {
    MeshAsset mesh;
    const float step = (to - from) / static_cast<float>(cells);
    for (int z = 0; z <= 1; ++z) {
        for (int x = 0; x <= cells; ++x) {
            const float at = from + static_cast<float>(x) * step;
            Vertex vertex;
            vertex.position = {at, height(at), z == 0 ? -3.0f : 3.0f};
            vertex.normal   = {0.0f, 1.0f, 0.0f};
            mesh.vertices.push_back(vertex);
        }
    }
    const uint32_t stride = static_cast<uint32_t>(cells + 1);
    for (uint32_t x = 0; x < static_cast<uint32_t>(cells); ++x) {
        mesh.indices.insert(mesh.indices.end(), {x, x + stride, x + 1, x + 1, x + stride, x + stride + 1});
    }
    return mesh;
}

// How far @p point is from the profile's polyline, in its X-Y plane: the mesh is flat across Z.
float distanceToProfile(Profile height, float from, float to, int cells, const glm::vec2& point) {
    const float step = (to - from) / static_cast<float>(cells);
    float best = std::numeric_limits<float>::max();
    for (int x = 0; x < cells; ++x) {
        const float     ax = from + static_cast<float>(x) * step;
        const glm::vec2 a  = {ax, height(ax)};
        const glm::vec2 b  = {ax + step, height(ax + step)};
        const float     t  = glm::clamp(glm::dot(point - a, b - a) / glm::dot(b - a, b - a), 0.0f, 1.0f);
        best = glm::min(best, glm::length(point - (a + (b - a) * t)));
    }
    return best;
}

struct GroundWalk {
    int   ticks    = 0;     ///< walked, after the run-up
    int   airborne = 0;     ///< of them not grounded
    int   thrown   = 0;     ///< of them `jumping`
    float above    = 0.0f;  ///< the capsule's furthest from the ground, m
    float below    = 0.0f;  ///< and furthest into it
};

// A character running at 4 m/s over @p height at @p tickRate, from a standing start on its flat
// run-up at x = 0 to x = 14; the ground's restitution is the default 0.2, which bounces a landing.
GroundWalk walkOver(Profile height, uint32_t tickRate) {
    constexpr float RADIUS = 0.3f, HALF = 0.6f;
    constexpr float FROM = -4.0f, TO = 20.0f;
    constexpr int   CELLS = 96;

    Scene scene;
    const EntityId ground = addMeshBody(scene, makeProfileMesh(height, FROM, TO, CELLS));
    scene.get<Rigidbody>(ground).restitution = 0.2f;
    const EntityId walker = addCharacter(scene, RADIUS, HALF, 0.4f);
    Transform& at = scene.get<Transform>(walker);
    at.position.y = height(0.0f) + HALF + RADIUS;
    CharacterController& cc = scene.get<CharacterController>(walker);
    cc.moveInput = glm::vec3(0.0f);

    TestFrame frame(scene, meshLibrary());
    frame.clock.setTickRate(tickRate);
    PhysicsSystem physics;
    CharacterControllerSystem controller;
    const auto tick = [&]() {
        physics.fixedUpdate(frame.ctx);
        controller.fixedUpdate(frame.ctx);
    };

    // Settled standing, then up to speed before the flat ends at x = 2.
    const int second = static_cast<int>(tickRate);
    for (int i = 0; i < second / 2; ++i) tick();
    cc.moveInput = {4.0f, 0.0f, 0.0f};
    for (int i = 0; i < second / 4; ++i) tick();

    GroundWalk walk;
    while (at.position.x < 14.0f && walk.ticks < 10 * second) {
        tick();
        ++walk.ticks;
        if (!cc.grounded) ++walk.airborne;
        if (cc.jumping) ++walk.thrown;
        // The bottom sphere's centre, a radius off the ground when it rests on it.
        const glm::vec2 foot = {at.position.x, at.position.y - HALF};
        const float     gap  = distanceToProfile(height, FROM, TO, CELLS, foot) - RADIUS;
        walk.above = glm::max(walk.above, gap);
        walk.below = glm::max(walk.below, -gap);
    }
    return walk;
}

float flatGround(float) {
    return 0.0f;
}

// Flat to x = 2, then down at 15 degrees: walked onto, the edge is a crest.
float descent(float x) {
    return x < 2.0f ? 0.0f : -(x - 2.0f) * std::tan(glm::radians(15.0f));
}

// Up at 12 degrees from x = 2, over a crest at x = 6, down at 12 to flat at x = 10.
float crest(float x) {
    const float rise = std::tan(glm::radians(12.0f));
    if (x < 2.0f || x > 10.0f) return 0.0f;
    return x < 6.0f ? (x - 2.0f) * rise : (10.0f - x) * rise;
}

// Rolling ground from x = 2: 15 cm high, 4 m from crest to crest, never steeper than 14 degrees.
float undulation(float x) {
    return x < 2.0f ? 0.0f : 0.15f * std::sin(glm::two_pi<float>() * (x - 2.0f) * 0.25f);
}

// A character's contact is real overlap, met at the pose the tick began at, so contact alone would
// fly it off every crest and skip it down every slope - each skip a jump clip in a game. Grounding
// holds it: on the ground every tick, never thrown, and its capsule within a centimetre and a half of
// it - a tick of a slope's fall at 64 Hz, or the solve's give at a slope's foot.
void testACharacterStaysOnRollingGround() {
    std::printf("A character running over slopes and crests:\n");

    const struct {
        const char* name;
        Profile     height;
    } grounds[] = {
        {"flat ground", flatGround},
        {"a 15 degree descent", descent},
        {"a 12 degree crest", crest},
        {"rolling ground", undulation},
    };
    for (const uint32_t rate : {Config::DEFAULT_TICK_RATE, 128u}) {
        for (const auto& ground : grounds) {
            const GroundWalk walk = walkOver(ground.height, rate);
            std::printf(
                "      %s at %u Hz: %d of %d ticks airborne, %d thrown, %.4f m above, %.4f m into it\n",
                ground.name,
                rate,
                walk.airborne,
                walk.ticks,
                walk.thrown,
                static_cast<double>(walk.above),
                static_cast<double>(walk.below)
            );
            check("  it is grounded every tick", walk.ticks > 0 && walk.airborne == 0);
            check("  never thrown off it", walk.thrown == 0);
            check("  and its feet stay on it", walk.above < 0.015f && walk.below < 0.015f);
        }
    }
}

// Landing from a fall strikes ground of restitution 0.2 faster than the solver's bounce threshold.
// The bounce is the contact's, on ground the character was not standing on: grounding takes it back,
// so it lands and stays down.
void testALandingDoesNotBounce() {
    std::printf("A character landing from a fall:\n");

    Scene scene;
    const EntityId ground = addMeshBody(scene, makeGridMesh(8, 16.0f));
    scene.get<Rigidbody>(ground).restitution = 0.2f;
    const EntityId walker = addCharacter(scene, 0.3f, 0.6f, 0.4f);
    Transform& at = scene.get<Transform>(walker);
    at.position.y += 1.5f;
    CharacterController& cc = scene.get<CharacterController>(walker);
    cc.moveInput = glm::vec3(0.0f);

    TestFrame frame(scene, meshLibrary());
    PhysicsSystem physics;
    CharacterControllerSystem controller;
    const auto tick = [&]() {
        physics.fixedUpdate(frame.ctx);
        controller.fixedUpdate(frame.ctx);
    };
    for (int i = 0; i < 128 && !cc.grounded; ++i) tick();
    check("it lands", cc.grounded);

    const float landed   = at.position.y;
    float       highest  = landed;
    int         airborne = 0;
    for (int i = 0; i < 64; ++i) {
        tick();
        highest = glm::max(highest, at.position.y);
        if (!cc.grounded) ++airborne;
    }
    std::printf("      %d ticks off the ground after it, rose %.4f m\n", airborne, highest - landed);
    check("  and stays down: no tick off the ground", airborne == 0);
    check("  nor any hop", highest - landed < 0.005f);
}

// What gameplay throws off the ground - a jump pad's launch - stays thrown: grounding takes back only
// what the ground itself gave.
void testAThrowOffTheGroundIsKept() {
    std::printf("A character launched off the ground by gameplay:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    const EntityId walker = addCharacter(scene, 0.3f, 0.5f, 0.4f);
    CharacterController& cc = scene.get<CharacterController>(walker);
    simulate(scene, 30);
    check("it stands", cc.grounded && !cc.jumping);

    const float from = scene.get<Transform>(walker).position.y;
    simulate(scene, 1, [&]() { scene.get<Rigidbody>(walker).linearVelocity.y = 8.0f; });
    const bool launched = cc.jumping && !cc.grounded;
    float highest = from;
    for (int i = 0; i < 160; ++i) {
        simulate(scene, 1);
        highest = glm::max(highest, scene.get<Transform>(walker).position.y);
    }
    std::printf("      rose %.3f m\n", highest - from);
    check("thrown, it is jumping and not grounded", launched);
    check("  and flies as high as the throw sends it", highest - from > 2.5f);
    check("  coming down to stand again", cc.grounded && !cc.jumping);

    // Its own jump, from where it stands: the floor it leaves is still in contact a tick later.
    const float stood = scene.get<Transform>(walker).position.y;
    cc.jumpRequested  = true;
    float top = stood;
    for (int i = 0; i < 64; ++i) {
        simulate(scene, 1);
        top = glm::max(top, scene.get<Transform>(walker).position.y);
    }
    std::printf("      jumped %.3f m\n", top - stood);
    check("a jump rises its jumpSpeed's height", top - stood > 1.1f);
}

// The same on a real terrain, rolling both ways: its triangles are rebuilt from heights, not
// authored, and walked across the grid's diagonals.
void testACharacterStaysOnATerrain() {
    std::printf("A character running across a rolling terrain:\n");

    constexpr int   RESOLUTION = 65;
    constexpr float RADIUS = 0.3f, HALF = 0.6f;
    TerrainAsset terrain;
    terrain.resolution = RESOLUTION;
    terrain.size       = 32.0f;
    terrain.heights.resize(RESOLUTION * RESOLUTION);
    terrain.weights.assign(RESOLUTION * RESOLUTION * 4, 0);
    for (int j = 0; j < RESOLUTION; ++j) {
        for (int i = 0; i < RESOLUTION; ++i) {
            const float x = static_cast<float>(i) * 0.5f - 16.0f;
            const float z = static_cast<float>(j) * 0.5f - 16.0f;
            const size_t at = static_cast<size_t>(j) * RESOLUTION + i;
            terrain.heights[at] = 0.4f * std::sin(0.5f * x) * std::cos(0.4f * z);
        }
    }
    const TerrainHandle handle = meshLibrary().add(TerrainAsset(terrain), "test:rolling-terrain");

    Scene scene;
    const EntityId ground = scene.createEntity();
    scene.add<Transform>(ground, Transform{});
    Rigidbody still;
    still.motion = RigidbodyMotion::Static;
    scene.add<Rigidbody>(ground, std::move(still));
    ColliderPart part;
    part.shape = ColliderShape::Terrain;
    Collider collider;
    collider.parts = {part};
    scene.add<Collider>(ground, std::move(collider));
    Terrain component;
    component.terrain = handle;
    scene.add<Terrain>(ground, std::move(component));

    const EntityId walker = addCharacter(scene, RADIUS, HALF, 0.4f);
    Transform& at = scene.get<Transform>(walker);
    at.position = {-12.0f, terrain.heightAt(-12.0f, -4.0f) + HALF + RADIUS, -4.0f};
    CharacterController& cc = scene.get<CharacterController>(walker);
    cc.moveInput = glm::vec3(0.0f);

    TestFrame frame(scene, meshLibrary());
    frame.clock.setTickRate(128);
    PhysicsSystem physics;
    CharacterControllerSystem controller;
    const auto tick = [&]() {
        physics.fixedUpdate(frame.ctx);
        controller.fixedUpdate(frame.ctx);
    };
    for (int i = 0; i < 64; ++i) tick();
    cc.moveInput = glm::normalize(glm::vec3(1.0f, 0.0f, 0.4f)) * 4.0f;
    for (int i = 0; i < 32; ++i) tick();

    int   airborne = 0;
    float above    = 0.0f;
    float below    = 0.0f;
    for (int i = 0; i < 4 * 128; ++i) {
        tick();
        if (!cc.grounded || cc.jumping) ++airborne;
        // The capsule's lowest point over the surface beneath it: on a slope it sits a little above.
        const float rise = at.position.y - HALF - RADIUS - terrain.heightAt(at.position.x, at.position.z);
        above = glm::max(above, rise);
        below = glm::max(below, -rise);
    }
    std::printf(
        "      reached x = %.1f; %d ticks off the ground, %.4f m above it, %.4f m into it\n",
        static_cast<double>(at.position.x),
        airborne,
        static_cast<double>(above),
        static_cast<double>(below)
    );
    check("it crosses the terrain", at.position.x > 2.0f);
    check("  grounded every tick", airborne == 0);
    check("  its feet on it", above < 0.02f && below < 0.005f);
}

} // namespace

void runPhysicsCharacterTests() {
    testAHeavierCrateIsHarderToPush();
    testAShoveCarriesWithKnockbackDrag();
    testLettingGoIsNotAShove();
    testAKinematicCharacterIsLeftAlone();
    testADozingCharacterWalksWhenAsked();
    testACharacterClimbsAWall();
    testACrouchPassesUnderABeamAndStandsOnlyWithRoom();
    testACrouchStandsUnderWhatDoesNotCollideWithIt();
    testACharacterRidesWhatItStandsOn();
    testARiderTakesTheSpeedOfTheSurfaceUnderIt();
    testCharacterStaircase();
    testCharacterStepUp();
    testAJumpDuringAStepIsAWholeJump();
    testStepUpPastOwnBones();
    testACharacterStaysOnRollingGround();
    testALandingDoesNotBounce();
    testAThrowOffTheGroundIsKept();
    testACharacterStaysOnATerrain();
}
