#include "physics/physics_support.h"

#include "ecs/component/physics/joint.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "system/physics/physics_system.h"

namespace {

void testJoints() {
    std::printf("Joints:\n");

    // A point joint to a fixed anchor, with gravity pulling for three seconds.
    Scene point;
    const EntityId post = addAnchor(point, {0.0f, 5.0f, 0.0f});
    const EntityId hung = addFallingBody(point, {0.0f, 4.0f, 0.0f}, 0.25f);
    {
        Joint joint;
        joint.type = JointType::Point;
        joint.connected = post;
        point.add<Joint>(hung, std::move(joint));
    }

    simulate(point, 180);
    const glm::vec3 held = point.get<Transform>(hung).position;
    check("a point joint holds a body against gravity", std::fabs(held.y - 5.0f) < 0.25f);
    check("  at the anchor, not merely near it", glm::length(held - glm::vec3(0.0f, 5.0f, 0.0f)) < 0.25f);

    // Without the joint the body is on the floor by now, so the joint did the holding.
    Scene loose;
    addAnchor(loose, {0.0f, 5.0f, 0.0f});
    const EntityId dropped = addFallingBody(loose, {0.0f, 4.0f, 0.0f}, 0.25f);
    simulate(loose, 180);
    check("the same body without one falls", loose.get<Transform>(dropped).position.y < 0.0f);

    // The documented world pin: connected has a pose but no Rigidbody, and the joint
    // holds to that point. A solver that skipped the pair would drop it silently.
    Scene pinned;
    const EntityId mark = pinned.createEntity();
    {
        Transform t;
        t.position = {0.0f, 5.0f, 0.0f};
        pinned.add<Transform>(mark, std::move(t));
    }
    const EntityId strung = addFallingBody(pinned, {0.0f, 4.0f, 0.0f}, 0.25f);
    {
        Joint joint;
        joint.type = JointType::Point;
        joint.connected = mark;
        pinned.add<Joint>(strung, std::move(joint));
    }
    simulate(pinned, 180);
    check(
        "a joint holds to an entity that has no body of its own",
        glm::length(pinned.get<Transform>(strung).position - glm::vec3(0.0f, 5.0f, 0.0f)) < 0.25f
    );

    // A distance joint frees everything but the distance, so a body hung off-centre
    // swings down to end below the anchor at the given length.
    Scene rope;
    const EntityId top = addAnchor(rope, {0.0f, 5.0f, 0.0f});
    const EntityId bob = addFallingBody(rope, {2.0f, 5.0f, 0.0f}, 0.25f);
    {
        Joint joint;
        joint.type = JointType::Distance;
        joint.connected = top;
        joint.distance = 2.0f;
        rope.add<Joint>(bob, std::move(joint));
    }

    // Sampled throughout: an undamped pendulum never stops, so one tick's height is
    // only a phase of the swing.
    float lowest = 5.0f;
    float worstSpan = 0.0f;
    for (int step = 0; step < 30; ++step) {
        simulate(rope, 10);
        const glm::vec3 at = rope.get<Transform>(bob).position;
        lowest = glm::min(lowest, at.y);
        const float span = glm::length(at - glm::vec3(0.0f, 5.0f, 0.0f));
        worstSpan = glm::max(worstSpan, std::fabs(span - 2.0f));
    }
    check("a distance joint keeps its length through the swing", worstSpan < 0.3f);
    check("  while letting the body swing", lowest < 4.0f);
}

// A hanging chain's top joint carries every link below it. Found from zero every
// tick, the passes never arrive and the chain hangs stretched - why a stack's
// contacts carry impulses between ticks. A joint is a soft spring, so some stretch
// is stiffness; the rest would be the restart.
void testAHangingChainHoldsItsLength() {
    std::printf("A chain hanging from a post:\n");

    constexpr int   LINKS = 10;
    constexpr float PITCH = 0.3f;
    constexpr int   TICKS = 240;

    const auto hang = [&](Scene& scene) {
        EntityId previous = addAnchor(scene, {0.0f, 5.0f, 0.0f});
        EntityId last{};
        for (int i = 0; i < LINKS; ++i) {
            const float y = 5.0f - PITCH * static_cast<float>(i + 1);
            last = addFallingBody(scene, {0.0f, y, 0.0f}, 0.1f);
            Joint joint;
            joint.connected       = previous;
            joint.anchor          = {0.0f, PITCH * 0.5f, 0.0f};
            joint.connectedAnchor = i == 0 ? glm::vec3(0.0f) : glm::vec3(0.0f, -PITCH * 0.5f, 0.0f);
            scene.add<Joint>(last, std::move(joint));
            previous = last;
        }
        // Heavy at the end, like a lamp on a chain: the top joint's load.
        scene.get<Rigidbody>(last).mass = 50.0f;
        return last;
    };
    const auto stretchOf = [&](const Scene& scene, EntityId last) {
        const float hung = 5.0f - (scene.get<Transform>(last).position.y - PITCH * 0.5f);
        return hung - PITCH * static_cast<float>(LINKS);
    };

    // One system across the run, as the app has, so each tick starts from the last.
    Scene carried;
    const EntityId carriedEnd = hang(carried);
    simulate(carried, TICKS);

    // A system per tick, so every tick starts from nothing.
    Scene restarted;
    const EntityId restartedEnd = hang(restarted);
    for (int tick = 0; tick < TICKS; ++tick) simulate(restarted, 1);

    const float kept  = stretchOf(carried, carriedEnd);
    const float fresh = stretchOf(restarted, restartedEnd);
    std::printf(
        "      stretched %.4f m carried, %.4f m started from nothing, over %.1f m\n",
        static_cast<double>(kept),
        static_cast<double>(fresh),
        static_cast<double>(PITCH * LINKS)
    );
    check("a chain carrying its joints' impulses hangs far closer to its length", kept < 0.75f * fresh);
}

// Stiffness scaling each pass's impulse would deliver 1 - (1 - stiffness)^n: at
// eight passes half stiffness would be 99.6% of rigid, and the knob would change
// meaning with a scene-wide solver setting.
void testJointStiffnessIsIterationIndependent() {
    std::printf("Joint stiffness against the pass count:\n");

    auto driftAfter = [](float stiffness, int iterations) {
        Scene scene;
        scene.physics().gravity = glm::vec3(0.0f);   // the joint is the only force
        scene.physics().solverIterations = iterations;

        const EntityId anchor = scene.createEntity();
        Transform at;
        at.position = {0.0f, 0.0f, 0.0f};
        scene.add<Transform>(anchor, std::move(at));

        const EntityId body = scene.createEntity();
        Transform bodyAt;
        bodyAt.position = {2.0f, 0.0f, 0.0f};      // a metre past where it belongs
        scene.add<Transform>(body, std::move(bodyAt));
        Rigidbody rb;
        rb.mass = 1.0f;
        rb.canSleep = false;
        scene.add<Rigidbody>(body, std::move(rb));

        Joint joint;
        joint.type = JointType::Distance;
        joint.connected = anchor;
        joint.distance = 1.0f;
        joint.stiffness = stiffness;
        scene.add<Joint>(body, std::move(joint));

        // Few ticks on purpose: the closing rate differs, and enough ticks close any gap.
        simulate(scene, 3);
        return scene.get<Transform>(body).position.x - 1.0f;   // gap left
    };

    const float few  = driftAfter(0.25f, 2);
    const float many = driftAfter(0.25f, 16);
    check("half-closed at two passes and at sixteen agree", std::fabs(few - many) < 0.05f);

    // A stiffer joint still closes more of the gap.
    check("  and a stiffer joint closes more of the gap", driftAfter(0.8f, 8) < driftAfter(0.2f, 8) - 0.05f);
}

// A joint closes drift with a velocity spent on the poses before the relax passes
// take it back, as a contact's is. Never relaxed, the closing speed would carry
// into the next tick as uncaused motion: a ragdoll on a floor would jitter.
void testAJointClosesDriftWithoutKeepingTheSpeed() {
    std::printf("A joint closing a drift:\n");

    Scene scene;
    scene.physics().gravity = glm::vec3(0.0f);   // the joint is the only force

    const EntityId anchor = addAnchor(scene, {0.0f, 0.0f, 0.0f});
    const EntityId body = addFallingBody(scene, {2.0f, 0.0f, 0.0f}, 0.25f);
    Joint rope;
    rope.type = JointType::Distance;
    rope.connected = anchor;
    rope.distance = 1.0f;                        // a metre stretched
    scene.add<Joint>(body, std::move(rope));

    TestFrame frame(scene);
    PhysicsSystem physics;
    physics.fixedUpdate(frame.ctx);

    check("a stretched joint pulls the body in", scene.get<Transform>(body).position.x < 1.9f);
    check(
        "  and leaves none of that speed on it",
        glm::length(scene.get<Rigidbody>(body).linearVelocity) < 0.01f
    );
}

// A jointed pair has no manifold on purpose, so waking that walked only manifolds
// would leave a sleeper on a joint as fixed as a nail: the solver treats sleepers
// as immovable, and pulling the other end would do nothing.
void testJointWakesASleeper() {
    std::printf("A sleeper on the end of a joint:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});

    // Resting on the floor and asleep.
    const EntityId anchorBody = addFallingBody(scene, {0.0f, 0.4f, 0.0f}, 0.4f);
    const EntityId hauler = addFallingBody(scene, {2.0f, 0.4f, 0.0f}, 0.4f);

    Joint rope;
    rope.type = JointType::Distance;
    rope.connected = anchorBody;
    rope.distance = 2.0f;
    scene.add<Joint>(hauler, std::move(rope));

    simulate(scene, 240);

    // Put to sleep directly: a jointed body settles slowly, and the question is
    // whether hauling the far end wakes it, not how long resting takes.
    scene.get<Rigidbody>(anchorBody).sleeping = true;
    scene.get<Rigidbody>(anchorBody).linearVelocity = glm::vec3(0.0f);
    check("a body asleep on the end of a rope", scene.get<Rigidbody>(anchorBody).sleeping);

    // Haul on the far end, hard and away.
    scene.get<Rigidbody>(hauler).sleeping = false;
    scene.get<Rigidbody>(hauler).sleepTimer = 0.0f;
    scene.get<Rigidbody>(hauler).linearVelocity = {6.0f, 0.0f, 0.0f};

    const float before = scene.get<Transform>(anchorBody).position.x;
    simulate(scene, 60);
    const float after = scene.get<Transform>(anchorBody).position.x;

    check("  hauling one end wakes the other", !scene.get<Rigidbody>(anchorBody).sleeping);
    check("  and drags it along", after > before + 0.1f);
}

// A joint whose other end is its own body. A solve storing back copies of both
// bodies in turn would have the second store overwrite the first: half of every
// equal-and-opposite pair lost, and the body pushing itself along.
void testABodyJointedToItselfHoldsStill() {
    std::printf("A body jointed to itself:\n");

    Scene scene;
    scene.physics().gravity = glm::vec3(0.0f);   // the joint is the only force

    const EntityId body = addFallingBody(scene, {0.0f, 0.0f, 0.0f}, 0.25f);
    Joint joint;
    joint.connected       = body;
    joint.anchor          = {0.5f, 0.2f, 0.0f};
    joint.connectedAnchor = {-0.3f, 0.1f, 0.4f};
    scene.add<Joint>(body, std::move(joint));

    simulate(scene, 60);

    const Transform& at = scene.get<Transform>(body);
    const Rigidbody& rb = scene.get<Rigidbody>(body);
    const float moved = glm::length(at.position);
    std::printf("      moved %.4f m in a second\n", static_cast<double>(moved));
    check("it does not push itself anywhere", moved < 1e-5f && glm::length(rb.linearVelocity) < 1e-5f);
    check(
        "  nor turn itself",
        std::fabs(at.rotation.w) > 1.0f - 1e-6f && glm::length(rb.angularVelocity) < 1e-5f
    );
}

// `connected` may name an entity with no Rigidbody, and the joint then holds the
// body to a fixed world point - here with an unset distance the first tick measures.
void testDistanceJointPinnedToWorld() {
    std::printf("A rope pinned to a world point:\n");

    Scene scene;
    // A pose and nothing else: a world pin.
    const EntityId post = scene.createEntity();
    Transform postAt;
    postAt.position = {0.0f, 4.0f, 0.0f};
    scene.add<Transform>(post, std::move(postAt));

    const EntityId weight = scene.createEntity();
    Transform weightAt;
    weightAt.position = {0.0f, 1.5f, 0.0f};
    scene.add<Transform>(weight, std::move(weightAt));
    Rigidbody body;
    body.mass = 10.0f;
    scene.add<Rigidbody>(weight, std::move(body));
    ColliderPart part;
    part.shape = ColliderShape::Capsule;
    part.radius = 0.2f;
    part.halfHeight = 0.0f;
    Collider collider;
    collider.parts = { part };
    scene.add<Collider>(weight, std::move(collider));

    Joint rope;
    rope.type = JointType::Distance;
    rope.connected = post;
    rope.distance = -1.0f;         // measured on the first tick
    scene.add<Joint>(weight, std::move(rope));

    simulate(scene, 240);

    const float y = scene.get<Transform>(weight).position.y;
    check("a weight on an auto-length rope hangs where it started", std::fabs(y - 1.5f) < 0.15f);
    check(
        "  and the length it measured is recorded",
        std::fabs(scene.get<Joint>(weight).resolvedDistance - 2.5f) < 0.05f
    );
    // The measurement lands in resolvedDistance, so a saved scene still asks to be
    // measured rather than carrying a length nobody chose.
    check("  while the authored field still asks to be measured", scene.get<Joint>(weight).distance < 0.0f);
}

// Saved as raw scene slots, a prefab joint's references would point, in another
// scene, at whatever those slots hold there - a wall, a crate, or nothing.
void testPrefabKeepsItsJoints() {
    std::printf("Prefab round-trip of a joint:\n");

    Scene authored;
    const EntityId anchor = authored.createEntity();
    {
        Transform t;
        t.position = {0.0f, 5.0f, 0.0f};
        authored.add<Transform>(anchor, std::move(t));
        Rigidbody rb;
        rb.motion = RigidbodyMotion::Static;
        authored.add<Rigidbody>(anchor, std::move(rb));
    }
    const EntityId bob = authored.createEntity();
    {
        Transform t;
        t.position = {0.0f, 3.0f, 0.0f};
        authored.add<Transform>(bob, std::move(t));
        Rigidbody rb;
        rb.mass = 10.0f;
        authored.add<Rigidbody>(bob, std::move(rb));
        Joint joint;
        joint.type = JointType::Point;
        joint.connected = anchor;
        authored.add<Joint>(bob, std::move(joint));
    }
    HierarchyOperations::setParent(authored, bob, anchor);

    ResourceManager resources;
    const std::string path =
        (runScratch() / "vkm_joint_prefab.json").string();
    check("the prefab saves", Prefab::save(authored, anchor, path, resources));

    // Slots already taken, so a raw slot reference would land on a decoy.
    Scene target;
    for (int i = 0; i < 4; ++i) {
        const EntityId decoy = target.createEntity();
        target.add<Transform>(decoy, Transform{});
    }
    const EntityId instance = Prefab::instantiate(target, resources, path);
    check("and instantiates elsewhere", static_cast<bool>(instance));

    EntityId hung = {};
    target.forEach<Joint>([&](EntityId id, const Joint&) { hung = id; });
    check("the joint came along", static_cast<bool>(hung));
    check(
        "  tied to the instance's own anchor, not a slot",
        hung && target.get<Joint>(hung).connected == instance
    );

    std::filesystem::remove(path);
}

// A 5 kg box a metre out from a fixed post, its near face on the joint: level, it asks 49 N m to hold.
EntityId addArm(Scene& scene, float holdTorque) {
    const EntityId post = addAnchor(scene, {0.0f, 5.0f, 0.0f});
    const EntityId arm = addFallingBody(scene, {1.0f, 5.0f, 0.0f}, 0.25f);
    Joint joint;
    joint.type = JointType::Point;
    joint.connected = post;
    joint.anchor = {-1.0f, 0.0f, 0.0f};
    joint.holdTorque = holdTorque;
    scene.add<Joint>(arm, std::move(joint));
    return arm;
}

// The lowest the arm's centre reaches over two seconds.
float lowestOver(Scene& scene, EntityId arm) {
    float lowest = scene.get<Transform>(arm).position.y;
    for (int step = 0; step < 24; ++step) {
        simulate(scene, 10);
        lowest = glm::min(lowest, scene.get<Transform>(arm).position.y);
    }
    return lowest;
}

void testAHeldJointKeepsItsAngle() {
    std::printf("Joint hold torque:\n");

    Scene strong;
    const EntityId held = addArm(strong, 100.0f);
    check("a hold stronger than the load keeps an arm level", lowestOver(strong, held) > 4.8f);

    Scene weak;
    const EntityId sags = addArm(weak, 20.0f);
    check("  one weaker than the load gives way", lowestOver(weak, sags) < 4.3f);

    Scene free;
    const EntityId swings = addArm(free, 0.0f);
    check("  and none leaves the rotation free", lowestOver(free, swings) < 4.1f);

    // Held level, then made kinematic so neither end can move, turned 45 degrees down and let go:
    // the hold keeps the new angle, not the first.
    Scene turned;
    const EntityId arm = addArm(turned, 100.0f);
    simulate(turned, 10);
    turned.get<Rigidbody>(arm).motion = RigidbodyMotion::Kinematic;
    simulate(turned, 10);
    const float half = glm::quarter_pi<float>();
    const glm::vec3 down = {std::cos(half), 5.0f - std::sin(half), 0.0f};
    Transform& pose = turned.get<Transform>(arm);
    pose.position = down;
    pose.rotation = glm::angleAxis(-half, glm::vec3(0.0f, 0.0f, 1.0f));
    turned.get<Rigidbody>(arm).motion = RigidbodyMotion::Dynamic;
    simulate(turned, 120);
    check(
        "  the angle held is the one the pair had when it began to move",
        glm::length(turned.get<Transform>(arm).position - down) < 0.15f
    );
}

} // namespace

void runPhysicsJointTests() {
    testAHeldJointKeepsItsAngle();
    testJoints();
    testJointStiffnessIsIterationIndependent();
    testAHangingChainHoldsItsLength();
    testAJointClosesDriftWithoutKeepingTheSpeed();
    testJointWakesASleeper();
    testABodyJointedToItselfHoldsStill();
    testDistanceJointPinnedToWorld();
    testPrefabKeepsItsJoints();
}
