#include "physics/physics_support.h"

#include "ecs/component/physics/character_controller.h"
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

void testAHeavierCrateIsHarderToPush() {
    std::printf("A character pushing crates of different mass:\n");
    const float light = carriedBy(8.0f);
    const float heavy = carriedBy(800.0f);
    std::printf("      8 kg carried %.3f m, 800 kg carried %.3f m\n", light, heavy);
    check("the light crate is pushed", light > 0.5f);
    check("  and the heavy one far less", heavy < light * 0.25f);
}

} // namespace

void runPhysicsCharacterTests() {
    testAHeavierCrateIsHarderToPush();
    testCharacterStaircase();
    testCharacterStepUp();
    testAJumpDuringAStepIsAWholeJump();
    testStepUpPastOwnBones();
}
