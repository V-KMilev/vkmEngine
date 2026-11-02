// What touches what: triggers, layers, joints, contact events, sleep, fast bodies, and the same
// answer every run.

#include <cstring>
#include <vector>

#include "core/frame/physics_events.h"
#include "ecs/component/physics/joint.h"
#include "system/physics/physics_system.h"

#include "harness/check.h"
#include "harness/world.h"
#include "physics/bodies.h"

using namespace Vkm::Test;

namespace {

struct Seen {
    std::vector<CollisionEvent> collisions;
    std::vector<TriggerEvent>   triggers;
};

/// What the game's bus hears from physics, kept in order.
void listen(Game& game, Seen& seen) {
    game.events().subscribe<CollisionEvent>(
        [&seen](const CollisionEvent& e) { seen.collisions.push_back(e); }
    );
    game.events().subscribe<TriggerEvent>([&seen](const TriggerEvent& e) { seen.triggers.push_back(e); });
}

int count(const Seen& seen, EntityId trigger, EntityId other, ContactPhase phase) {
    int n = 0;
    for (const TriggerEvent& e : seen.triggers) n += e.trigger == trigger && e.other == other && e.phase == phase;
    return n;
}

} // namespace

TEST("a trigger reports what enters, stays and leaves, pushes nothing, and never senses the floor") {
    Game game;
    Scene& scene = game.scene();
    const EntityId floor = addFloor(scene);
    // A static zone resting on the floor, and a crate falling through it to the floor.
    const EntityId zone = addBody(
        scene,
        {0.0f, 1.0f, 0.0f},
        box({2.0f, 1.0f, 2.0f}),
        RigidbodyMotion::Static
    );
    scene.get<Collider>(zone).isTrigger = true;
    const EntityId crate = addCrate(scene, {0.0f, 5.0f, 0.0f});
    Seen seen;
    listen(game, seen);

    game.run(120);
    CHECK_NEAR(positionOf(scene, crate).y, 0.5f, 0.01f);
    CHECK(count(seen, zone, crate, ContactPhase::Began) == 1);
    CHECK(count(seen, zone, crate, ContactPhase::Stayed) > 0);
    CHECK(count(seen, zone, floor, ContactPhase::Began) == 0);
    for (const CollisionEvent& e : seen.collisions) CHECK((e.a != zone && e.b != zone));
}

TEST("a trigger senses a body leaving it and one destroyed inside it") {
    Game game;
    Scene& scene = game.scene();
    const EntityId zone = addBody(scene, {0.0f, 0.0f, 0.0f}, box(glm::vec3(1.0f)), RigidbodyMotion::Static);
    scene.get<Collider>(zone).isTrigger = true;
    const EntityId passing = addBody(scene, {-3.0f, 0.0f, 0.0f}, capsule(0.25f, 0.0f));
    const EntityId doomed  = addBody(scene, {0.0f, 0.0f, 0.6f}, capsule(0.25f, 0.0f));
    for (const EntityId id : {passing, doomed}) {
        bodyOf(scene, id).gravityScale  = 0.0f;
        bodyOf(scene, id).linearDamping = 0.0f;
        bodyOf(scene, id).canSleep      = false;
    }
    bodyOf(scene, passing).linearVelocity = {6.0f, 0.0f, 0.0f};
    Seen seen;
    listen(game, seen);

    game.run(90, [&](int frame) {
        if (frame == 30) scene.destroyEntity(doomed);
    });

    CHECK(count(seen, zone, passing, ContactPhase::Began) == 1);
    CHECK(count(seen, zone, passing, ContactPhase::Ended) == 1);
    CHECK(count(seen, zone, doomed, ContactPhase::Began) == 1);
    CHECK(count(seen, zone, doomed, ContactPhase::Ended) == 1);
    CHECK(positionOf(scene, passing).x > 5.0f);
}

TEST("bodies whose layers exclude each other pass through, and a layer of none collides with nothing") {
    Game game;
    Scene& scene = game.scene();
    const EntityId floor = addFloor(scene);
    bodyOf(scene, floor).collidesWith = ~0 & ~(1 << 2);
    const EntityId ghost = addCrate(scene, {-3.0f, 2.0f, 0.0f});
    const EntityId solid = addCrate(scene, {0.0f, 2.0f, 0.0f});
    const EntityId none  = addCrate(scene, {3.0f, 2.0f, 0.0f});
    bodyOf(scene, ghost).layer = 1 << 2;
    bodyOf(scene, solid).layer = 1 << 3;
    bodyOf(scene, none).layer  = 0;

    game.run(90);

    CHECK(positionOf(scene, ghost).y < -2.0f);
    CHECK_NEAR(positionOf(scene, solid).y, 0.5f, 0.01f);
    CHECK(positionOf(scene, none).y < -2.0f);
}

TEST("two jointed bodies collide only when their joint says so") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    // Each pair overlaps by half a crate, held together at their centers' midpoint.
    auto pair = [&](float z, bool collide) {
        const EntityId a = addCrate(scene, {0.0f, 0.5f, z});
        const EntityId b = addCrate(scene, {0.5f, 0.5f, z});
        Joint joint;
        joint.type             = JointType::Distance;
        joint.connected        = b;
        joint.collideConnected = collide;
        scene.add(a, joint);
        return std::pair{a, b};
    };
    const auto [apartA, apartB]   = pair(-3.0f, false);
    const auto [pushedA, pushedB] = pair(3.0f, true);
    Seen seen;
    listen(game, seen);

    game.run(60);

    auto touched = [&](EntityId a, EntityId b) {
        for (const CollisionEvent& e : seen.collisions) {
            if ((e.a == a && e.b == b) || (e.a == b && e.b == a)) return true;
        }
        return false;
    };
    CHECK(!touched(apartA, apartB));
    CHECK(touched(pushedA, pushedB));
    CHECK_NEAR(positionOf(scene, apartB).x - positionOf(scene, apartA).x, 0.5f, 0.01f);
}

TEST("contacts begin, stay and end in slot order, each pair named the same way every tick") {
    Game game;
    Scene& scene = game.scene();
    const EntityId floor = addFloor(scene);
    std::vector<EntityId> crates;
    for (int i = 0; i < 4; ++i) crates.push_back(addCrate(scene, {i * 2.0f - 3.0f, 0.5f, 0.0f}));
    Seen seen;
    listen(game, seen);

    game.run(20, [&](int frame) {
        if (frame == 10) scene.destroyEntity(crates[1]);
    });

    std::vector<CollisionEvent> began, ended;
    for (const CollisionEvent& e : seen.collisions) {
        CHECK(e.a.slot() < e.b.slot());
        if (e.phase == ContactPhase::Began) began.push_back(e);
        if (e.phase == ContactPhase::Ended) ended.push_back(e);
    }
    REQUIRE(began.size() == 4);
    for (size_t i = 0; i < began.size(); ++i) {
        CHECK(began[i].a == floor);
        CHECK(began[i].b == crates[i]);
        CHECK_NEAR(began[i].normal, glm::vec3(0.0f, 1.0f, 0.0f), 1e-3f);
    }
    REQUIRE(ended.size() == 1);
    CHECK(ended[0].b == crates[1]);
}

TEST("a landing is heard with how hard it struck, and each body hears it from its own side") {
    Game game;
    Scene& scene = game.scene();
    const EntityId floor = addFloor(scene);
    const EntityId crate = addCrate(scene, {0.0f, 5.5f, 0.0f});
    Seen seen;
    listen(game, seen);

    game.run(90);

    REQUIRE(!seen.collisions.empty());
    const CollisionEvent& landing = seen.collisions.front();
    // A 5 m drop lands at sqrt(2 g h) = 9.9 m/s.
    CHECK_NEAR(landing.speed, 9.9f, 0.3f);
    CHECK_NEAR(Collision::seenBy(landing, crate).normal, glm::vec3(0.0f, 1.0f, 0.0f), 1e-3f);
    CHECK_NEAR(Collision::seenBy(landing, floor).normal, glm::vec3(0.0f, -1.0f, 0.0f), 1e-3f);
    CHECK(Collision::seenBy(landing, floor).other == crate);
}

TEST("a body publishes what holds it up and what blocks it") {
    Game game;
    Scene& scene = game.scene();
    const EntityId floor = addFloor(scene);
    const EntityId wall  = addBody(
        scene,
        {1.0f, 1.0f, 0.0f},
        box({0.5f, 1.0f, 2.0f}),
        RigidbodyMotion::Static
    );
    const EntityId crate = addCrate(scene, {0.0f, 0.5f, 0.0f});
    const EntityId alone = addCrate(scene, {-6.0f, 0.5f, 0.0f});
    const EntityId air   = addCrate(scene, {-6.0f, 50.0f, 0.0f});
    bodyOf(scene, crate).canSleep = false;

    // Pressed against the wall every tick.
    game.run(30, [&](int) { bodyOf(scene, crate).linearVelocity.x = 1.0f; });

    const Rigidbody& pressed = bodyOf(scene, crate);
    CHECK(pressed.supported);
    CHECK(pressed.supportBody == floor);
    CHECK_NEAR(pressed.supportNormal, glm::vec3(0.0f, 1.0f, 0.0f), 1e-3f);
    CHECK_NEAR(pressed.blockNormal, glm::vec3(-1.0f, 0.0f, 0.0f), 1e-3f);
    CHECK(bodyOf(scene, alone).supported);
    CHECK_NEAR(bodyOf(scene, alone).blockNormal, glm::vec3(0.0f, 1.0f, 0.0f), 1e-3f);
    CHECK(!bodyOf(scene, air).supported);
    CHECK(!bodyOf(scene, air).supportBody);
    (void)wall;
}

namespace {

/// Every body's pose and velocity, as bytes.
std::string stateOf(Scene& scene) {
    std::string bytes;
    scene.forEach<Transform, Rigidbody>([&](EntityId, Transform& t, Rigidbody& b) {
        bytes.append(reinterpret_cast<const char*>(&t.position), sizeof(t.position));
        bytes.append(reinterpret_cast<const char*>(&t.rotation), sizeof(t.rotation));
        bytes.append(reinterpret_cast<const char*>(&b.linearVelocity), sizeof(b.linearVelocity));
        bytes.append(reinterpret_cast<const char*>(&b.angularVelocity), sizeof(b.angularVelocity));
    });
    return bytes;
}

/// A restless pile: crates and capsules tumbling onto each other and a ramp.
std::string tumble() {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    addBody(
        scene,
        {0.0f, 1.0f, 0.0f},
        box({3.0f, 0.2f, 3.0f}),
        RigidbodyMotion::Static,
        glm::angleAxis(0.3f, glm::vec3(0.0f, 0.0f, 1.0f))
    );
    for (int i = 0; i < 60; ++i) {
        const glm::vec3 at(std::sin(i * 1.7f) * 2.0f, 3.0f + i * 0.6f, std::cos(i * 2.3f) * 2.0f);
        const glm::quat turn = glm::angleAxis(i * 0.4f, glm::normalize(glm::vec3(1.0f, i % 3, 0.5f)));
        addBody(
            scene,
            at,
            i % 2 ? box(glm::vec3(0.3f, 0.2f, 0.4f)) : capsule(0.2f, 0.3f),
            RigidbodyMotion::Dynamic,
            turn
        );
    }
    game.run(300);
    return stateOf(scene);
}

} // namespace

TEST("the same world run twice ends bit-identical, on the pool's threads and on one") {
    const std::string first = tumble();
    // The first run ended the pool (Engine::run), so this one runs every parallel loop on this thread.
    const std::string second = tumble();
    REQUIRE(first.size() == second.size());
    CHECK(std::memcmp(first.data(), second.data(), first.size()) == 0);
}

TEST("a resting pile allocates nothing a tick") {
    Frame frame;
    Scene& scene = frame.scene;
    addFloor(scene);
    for (int i = 0; i < 20; ++i) {
        const EntityId crate = addCrate(scene, {(i % 5) * 1.1f, 0.5f + (i / 5), 0.0f});
        bodyOf(scene, crate).canSleep = false;
    }
    PhysicsSystem physics;
    frame.tick(physics, 120);

    const uint64_t before = allocations();
    frame.tick(physics, 100);
    CHECK(allocations() - before == 0);
}

TEST("a body left alone falls asleep with its island, and a touch wakes the island") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    std::vector<EntityId> stack;
    for (int i = 0; i < 5; ++i) stack.push_back(addCrate(scene, {0.0f, 0.5f + i, 0.0f}));
    const EntityId dropped = addCrate(scene, {0.0f, 30.0f, 0.0f});
    bodyOf(scene, dropped).gravityScale = 0.0f;

    bool allAsleep = false;
    bool allWoken  = true;
    game.run(300, [&](int frame) {
        if (frame == 240) {
            allAsleep = true;
            for (const EntityId crate : stack) allAsleep = allAsleep && bodyOf(scene, crate).sleeping;
            bodyOf(scene, dropped).gravityScale = 1.0f;
            Rigidbody::wake(bodyOf(scene, dropped));
            scene.get<Transform>(dropped).position.y = 6.0f;
        }
        if (frame == 270) {
            for (const EntityId crate : stack) allWoken = allWoken && !bodyOf(scene, crate).sleeping;
        }
    });

    CHECK(allAsleep);
    CHECK(allWoken);
}

TEST("a sleeper set moving wakes, and one whose floor goes wakes and falls") {
    Game game;
    Scene& scene = game.scene();
    const EntityId floor  = addFloor(scene);
    const EntityId pushed = addCrate(scene, {-3.0f, 0.5f, 0.0f});
    const EntityId shelf  = addBody(
        scene,
        {3.0f, 2.0f, 0.0f},
        box({1.0f, 0.1f, 1.0f}),
        RigidbodyMotion::Static
    );
    const EntityId onShelf = addCrate(scene, {3.0f, 2.6f, 0.0f});

    bool asleep = false;
    game.run(300, [&](int frame) {
        if (frame == 200) {
            asleep = bodyOf(scene, pushed).sleeping && bodyOf(scene, onShelf).sleeping;
            bodyOf(scene, pushed).linearVelocity = {3.0f, 0.0f, 0.0f};
            scene.destroyEntity(shelf);
        }
    });

    CHECK(asleep);
    CHECK(positionOf(scene, pushed).x > -2.5f);
    CHECK_NEAR(positionOf(scene, onShelf).y, 0.5f, 0.02f);
    (void)floor;
}

TEST("a body that may not sleep keeps its island awake") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    const EntityId restless = addCrate(scene, {0.0f, 0.5f, 0.0f});
    const EntityId onTop    = addCrate(scene, {0.0f, 1.5f, 0.0f});
    const EntityId apart    = addCrate(scene, {4.0f, 0.5f, 0.0f});
    bodyOf(scene, restless).canSleep = false;

    game.run(400);

    CHECK(!bodyOf(scene, restless).sleeping);
    CHECK(!bodyOf(scene, onTop).sleeping);
    CHECK(bodyOf(scene, apart).sleeping);
}

TEST("a body spinning in place does not fall asleep, however still its center") {
    Game game;
    Scene& scene = game.scene();
    bodyOf(scene, addFloor(scene)).friction = 0.0f;
    const EntityId top = addBody(scene, {0.0f, 0.5f, 0.0f}, box({1.0f, 0.5f, 1.0f}));
    Rigidbody& body    = bodyOf(scene, top);
    body.friction        = 0.0f;
    body.angularDamping  = 0.0f;
    body.angularVelocity = {0.0f, 2.0f, 0.0f};

    game.run(240);

    CHECK(!bodyOf(scene, top).sleeping);
    CHECK_NEAR(bodyOf(scene, top).angularVelocity.y, 2.0f, 0.05f);
}

TEST("a fast body stops at a thin wall") {
    Game game;
    Scene& scene = game.scene();
    addBody(scene, {0.0f, 0.0f, 0.0f}, box({0.01f, 2.0f, 2.0f}), RigidbodyMotion::Static);
    const EntityId shot = addBody(scene, {-5.0f, 0.0f, 0.0f}, capsule(0.05f, 0.0f));
    bodyOf(scene, shot).gravityScale   = 0.0f;
    bodyOf(scene, shot).linearVelocity = {300.0f, 0.0f, 0.0f};

    game.run(10);

    CHECK(positionOf(scene, shot).x < 0.0f);
    CHECK(positionOf(scene, shot).x > -0.2f);
}

TEST("a fast body passes what it would not collide with: a trigger, an ignored layer, its jointed post") {
    Game game;
    Scene& scene = game.scene();
    auto wall = [&](float z) {
        return addBody(scene, {0.0f, 0.0f, z}, box({0.01f, 1.0f, 1.0f}), RigidbodyMotion::Static);
    };
    auto shot = [&](float z) {
        const EntityId id = addBody(scene, {-5.0f, 0.0f, z}, capsule(0.05f, 0.0f));
        bodyOf(scene, id).gravityScale   = 0.0f;
        bodyOf(scene, id).linearDamping  = 0.0f;
        bodyOf(scene, id).linearVelocity = {300.0f, 0.0f, 0.0f};
        return id;
    };
    const EntityId trigger = wall(-6.0f);
    scene.get<Collider>(trigger).isTrigger = true;
    const EntityId intoTrigger = shot(-6.0f);
    const EntityId layered     = wall(0.0f);
    bodyOf(scene, layered).collidesWith = 1;
    const EntityId intoLayer = shot(0.0f);
    bodyOf(scene, intoLayer).layer = 1 << 4;
    // Tethered from high above the post, so its flight barely pulls on the tether.
    const EntityId post     = wall(6.0f);
    const EntityId tethered = shot(6.0f);
    Joint joint;
    joint.type            = JointType::Distance;
    joint.connected       = post;
    joint.connectedAnchor = {0.0f, 100.0f, 0.0f};
    scene.add(tethered, joint);

    game.run(4);

    CHECK(positionOf(scene, intoTrigger).x > 5.0f);
    CHECK(positionOf(scene, intoLayer).x > 5.0f);
    CHECK(positionOf(scene, tethered).x > 5.0f);
}

TEST("a fast body sliding along a floor slides on") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    const EntityId puck = addBody(scene, {-15.0f, 0.1f, 0.0f}, capsule(0.1f, 0.0f));
    bodyOf(scene, puck).friction       = 0.0f;
    bodyOf(scene, puck).linearDamping  = 0.0f;
    bodyOf(scene, puck).linearVelocity = {60.0f, 0.0f, 0.0f};
    bodyOf(scene, puck).canSleep       = false;

    game.run(20);

    CHECK(positionOf(scene, puck).x > 4.0f);
    CHECK_NEAR(bodyOf(scene, puck).linearVelocity.x, 60.0f, 1.0f);
}
