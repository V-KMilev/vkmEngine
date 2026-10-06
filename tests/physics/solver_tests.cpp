#include "physics/physics_support.h"
#include "net/net_support.h"

#include <algorithm>
#include <exception>
#include <future>
#include <vector>

#include "debug/engine_error_log.h"
#include "platform/threading/thread_pool.h"

#include "system/physics/solver/solver.h"
#include "system/physics/solver/solver_math.h"
#include "system/physics/solver/contact_cache.h"
#include "ecs/component/physics/joint.h"
#include "io/scene/scene_serializer.h"
#include "system/physics/physics_events.h"
#include "system/physics/physics_system.h"

namespace {

// A simulation must be a function of the world, not its assembly order. SparseSet is
// swap-and-pop, so a storage walk carries insertion history, and Gauss-Seidel is
// order-dependent. PhysicsSystem canonicalises on entity slot; this test holds it.
void testTheSameWorldSimulatesTheSameWay() {
    std::printf("A world assembled two different ways:\n");

    constexpr int STACKED = 12;

    // Entities created in the same order, so a slot names the same box in each; only
    // the component add order differs, which a dense walk follows.
    const auto place = [](Scene& scene, EntityId id, const glm::vec3& at) {
        Transform transform;
        transform.position = at;
        scene.add<Transform>(id, std::move(transform));

        Rigidbody body;
        body.mass     = 5.0f;
        body.canSleep = false;
        scene.add<Rigidbody>(id, std::move(body));

        ColliderPart part;
        part.shape       = ColliderShape::Box;
        part.halfExtents = glm::vec3(0.2f);
        Collider collider;
        collider.parts = { part };
        scene.add<Collider>(id, std::move(collider));
    };

    const auto build = [&](Scene& scene, bool reversed) {
        addBox(scene, {0.0f, -0.5f, 0.0f}, {12.0f, 0.5f, 12.0f});

        // Created first, together, so slots agree whatever order components arrive in.
        std::vector<EntityId> boxes;
        boxes.reserve(STACKED);
        for (int i = 0; i < STACKED; ++i) boxes.push_back(scene.createEntity());

        // One column, every aabbMin.x identical to the bit: boxes that miss make no
        // manifold to order, and boxes at different x are split before the tie-break.
        const auto placeOf = [](int i) {
            return glm::vec3(0.0f, 0.6f + 0.45f * static_cast<float>(i), 0.0f);
        };

        if (reversed) {
            for (int i = STACKED; i-- > 0;) place(scene, boxes[static_cast<size_t>(i)], placeOf(i));
        } else {
            for (int i = 0; i < STACKED; ++i) place(scene, boxes[static_cast<size_t>(i)], placeOf(i));
        }
    };

    Scene forwards, backwards;
    build(forwards, false);
    build(backwards, true);

    check("both worlds hold the same bodies", forwards.entityCount() == backwards.entityCount());

    // While still falling: at rest, two solve orders would agree anyway.
    simulate(forwards, 45);
    simulate(backwards, 45);

    size_t compared = 0;
    size_t differed = 0;
    size_t moving   = 0;
    float  worst    = 0.0f;
    forwards.forEachEntity([&](EntityId id) {
        if (!forwards.has<Rigidbody>(id) || !forwards.has<Transform>(id)) return;
        if (forwards.get<Rigidbody>(id).motion == RigidbodyMotion::Static) return;
        if (!backwards.isAlive(id) || !backwards.has<Transform>(id)) return;

        ++compared;
        if (glm::length(forwards.get<Rigidbody>(id).linearVelocity) > 0.01f) ++moving;

        // Compared exactly: the same arithmetic on the same machine agrees to the bit, or
        // the order is not canonical; "close enough" would pass a quiet divergence.
        const glm::vec3 a = forwards.get<Transform>(id).position;
        const glm::vec3 b = backwards.get<Transform>(id).position;
        const float apart = glm::length(a - b);
        if (apart > worst) worst = apart;

        // Velocity as well: two worlds can pass the same place heading somewhere different.
        const glm::vec3 va = forwards.get<Rigidbody>(id).linearVelocity;
        const glm::vec3 vb = backwards.get<Rigidbody>(id).linearVelocity;
        if (a != b || va != vb) ++differed;
    });

    check("  and every box was compared", compared == STACKED);
    check("  while they were still moving", moving > 0);
    check("a tick is a function of the world, not of how it was built", differed == 0);
    if (differed != 0) {
        std::printf(
            "      %zu of %zu differ, worst %.6f m\n",
            differed,
            compared,
            static_cast<double>(worst)
        );
    }
}

// Something of everything the solver does: a leaning stack that topples, capsules
// landing on it, a swinging jointed chain, and sleep.
void buildRestlessWorld(Scene& scene) {
    addBox(scene, {0.0f, -0.5f, 0.0f}, {10.0f, 0.5f, 10.0f});

    for (int i = 0; i < 6; ++i) {
        const float level = static_cast<float>(i);
        const EntityId box = addFallingBody(scene, {0.07f * level, 0.3f + 0.61f * level, 0.0f}, 0.3f);
        scene.get<Transform>(box).rotation =
            glm::angleAxis(0.11f * level, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
        scene.get<Rigidbody>(box).canSleep = true;
    }

    for (int i = 0; i < 2; ++i) {
        const EntityId capsule = addFallingBody(scene, {0.4f - 0.8f * i, 5.0f + i, 0.3f}, 0.3f);
        Collider& collider = scene.get<Collider>(capsule);
        collider.parts[0].shape      = ColliderShape::Capsule;
        collider.parts[0].radius     = 0.2f;
        collider.parts[0].halfHeight = 0.3f;
        scene.get<Transform>(capsule).rotation =
            glm::angleAxis(1.2f, glm::vec3(0.0f, 0.0f, 1.0f));
        scene.get<Rigidbody>(capsule).canSleep = true;
    }

    EntityId previous = addAnchor(scene, {4.0f, 4.0f, 0.0f});
    for (int i = 0; i < 4; ++i) {
        const EntityId link = addFallingBody(scene, {4.5f + 0.5f * i, 4.0f, 0.0f}, 0.15f);
        Joint joint;
        joint.connected       = previous;
        joint.anchor          = {-0.25f, 0.0f, 0.0f};
        joint.connectedAnchor = i == 0 ? glm::vec3(0.0f) : glm::vec3(0.25f, 0.0f, 0.0f);
        scene.add<Joint>(link, std::move(joint));
        previous = link;
    }
}

// Every body's pose, velocity and sleep state, hashed by bits in entity order. Not
// quantised: a quantisation step hides any difference smaller than itself.
uint64_t hashBodies(const Scene& scene) {
    uint64_t hash = 14695981039346656037ull;
    const auto mix = [&](const void* data, size_t size) {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    scene.forEachEntity([&](EntityId id) {
        const Rigidbody* body = scene.tryGet<Rigidbody>(id);
        const Transform* transform = scene.tryGet<Transform>(id);
        if (!body || !transform) return;
        mix(&transform->position, sizeof(transform->position));
        mix(&transform->rotation, sizeof(transform->rotation));
        mix(&body->linearVelocity, sizeof(body->linearVelocity));
        mix(&body->angularVelocity, sizeof(body->angularVelocity));
        mix(&body->sleeping, sizeof(body->sleeping));
    });
    return hash;
}

// @p source again in @p out, components added in reverse slot order: same entities at
// the same slots, every storage's dense order backwards.
void rebuildBackwards(const Scene& source, Scene& out) {
    std::vector<EntityId> entities;
    source.forEachEntity([&](EntityId id) { entities.push_back(id); });
    std::sort(entities.begin(), entities.end(), [](EntityId a, EntityId b) { return a.slot() < b.slot(); });
    for (EntityId id : entities) out.createEntityAt(id.slot());

    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        const EntityId from = *it;
        const EntityId to   = out.entityAt(from.slot());
        if (const Transform* at = source.tryGet<Transform>(from)) out.add(to, Transform(*at));
        if (const Rigidbody* body = source.tryGet<Rigidbody>(from)) out.add(to, Rigidbody(*body));
        if (const Collider* shape = source.tryGet<Collider>(from)) out.add(to, Collider(*shape));
        if (const Joint* joint = source.tryGet<Joint>(from)) {
            Joint copy = *joint;
            copy.connected = joint->connected ? out.entityAt(joint->connected.slot()) : EntityId{};
            out.add(to, std::move(copy));
        }
    }
}

// Replay and networking assume one tick run twice lands in the same place to the bit.
// The hash is printed so a change meant to leave the answer alone can be checked
// before and after.
void testTheSameWorldRunTwiceEndsBitIdentical() {
    std::printf("The same world run twice:\n");

    constexpr int TICKS = 300;

    Scene first;
    buildRestlessWorld(first);

    // The second is assembled backwards, so every storage walks it in reverse: the
    // answer is keyed on slot, not arrival order.
    Scene built;
    buildRestlessWorld(built);
    Scene second;
    rebuildBackwards(built, second);
    bool reversed = false;
    if (const auto* bodies = second.storage<Rigidbody>(); bodies && bodies->size() > 1) {
        reversed = bodies->keyAt(0) > bodies->keyAt(bodies->size() - 1);
    }
    check("the second world's bodies are stored backwards", reversed);

    // The third loads the second's saved document, as a joining client does, and must
    // simulate what the server does.
    ResourceManager resources;
    const std::string document = SceneSerializer::saveToString(second, resources);
    Scene loaded;
    ResourceManager loadedResources;
    const bool reloaded = SceneSerializer::loadFromString(document, loaded, loadedResources);
    check("the world saves and loads back", reloaded);

    simulate(first, TICKS);
    simulate(second, TICKS);
    simulate(loaded, TICKS);

    const uint64_t hash = hashBodies(first);
    std::printf("      state hash after %d ticks: %016llx\n", TICKS, static_cast<unsigned long long>(hash));

    bool asleep = false;
    bool moved  = false;
    first.forEachEntity([&](EntityId id) {
        const Rigidbody* body = first.tryGet<Rigidbody>(id);
        if (!body || body->motion != RigidbodyMotion::Dynamic) return;
        asleep = asleep || body->sleeping;
        moved  = moved || glm::length(body->linearVelocity) > 0.01f;
    });
    check("  something in it fell asleep", asleep);
    check("  and something is still moving", moved);

    check("the same world built backwards ends identical to the bit", hashBodies(second) == hash);
    check("and a run of the world loaded from its file does too", hashBodies(loaded) == hash);
}

// Many pairs a tick, three deep, boxes and capsules, on a mesh floor so the triangle
// walk is loaded too.
void buildBusyPile(Scene& scene) {
    addMeshBody(scene, makeGridMesh(12, 12.0f));

    constexpr int SIDE   = 8;
    constexpr int LAYERS = 3;
    const glm::vec3 axis = glm::normalize(glm::vec3(0.1f, 1.0f, 0.1f));
    for (int layer = 0; layer < LAYERS; ++layer) {
        for (int x = 0; x < SIDE; ++x) {
            for (int z = 0; z < SIDE; ++z) {
                const glm::vec3 at(
                    -2.2f + 0.63f * x + 0.13f * layer,
                    0.32f + 0.65f * layer,
                    -2.2f + 0.63f * z
                );
                const EntityId body = addFallingBody(scene, at, 0.3f);
                scene.get<Transform>(body).rotation =
                    glm::angleAxis(0.07f * static_cast<float>(x + 2 * z + 3 * layer), axis);
                scene.get<Rigidbody>(body).canSleep = true;
                if ((x + z + layer) % 4 != 0) continue;

                ColliderPart& part = scene.get<Collider>(body).parts[0];
                part.shape      = ColliderShape::Capsule;
                part.radius     = 0.2f;
                part.halfHeight = 0.2f;
            }
        }
    }
}

struct PileRun {
    uint64_t                    hash = 0;
    std::vector<CollisionEvent> collisions;       ///< Every tick's, in the order heard
    size_t                      busiestTick = 0;  ///< Most collisions heard in one tick
};

PileRun runBusyPile(int ticks) {
    Scene scene;
    buildBusyPile(scene);
    TestFrame frame(scene, meshLibrary());
    PhysicsSystem physics;

    PileRun run;
    size_t heard = 0;
    frame.events.subscribe<CollisionEvent>([&](const CollisionEvent& event) {
        run.collisions.push_back(event);
        ++heard;
    });
    for (int tick = 0; tick < ticks; ++tick) {
        heard = 0;
        physics.fixedUpdate(frame.ctx);
        frame.events.flush();
        run.busiestTick = std::max(run.busiestTick, heard);
    }
    run.hash = hashBodies(scene);
    return run;
}

// Which threads sweep the pairs is not part of the answer: a pool task runs parallelFor
// serially, so the pile stepped there and from the main thread must agree to the bit,
// bodies and events. The hash is printed as above.
void testABusyPileStepsTheSameOnAnyThread() {
    std::printf("A busy pile, stepped from a worker and from the main thread:\n");

    ThreadPool& pool = ThreadPool::get();
    if (pool.threadCount() == 0) {
        std::printf("      no workers - skipped\n");
        return;
    }

    constexpr int TICKS = 120;

    std::promise<PileRun> promise;
    std::future<PileRun> onWorker = promise.get_future();
    pool.addTask([&] {
        try {
            promise.set_value(runBusyPile(TICKS));
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
    });
    const PileRun forked = runBusyPile(TICKS);
    const PileRun serial = onWorker.get();

    std::printf(
        "      state hash after %d ticks: %016llx\n",
        TICKS,
        static_cast<unsigned long long>(forked.hash)
    );

    // At least three chunks of pairs, so the main thread's run really did fork.
    check(
        "the pile touches in more pairs a tick than two chunks hold",
        forked.busiestTick > 2 * NARROWPHASE_CHUNK_PAIRS
    );
    check("the pile ends identical to the bit", forked.hash == serial.hash);

    bool sameEvents = forked.collisions.size() == serial.collisions.size();
    for (size_t i = 0; sameEvents && i < forked.collisions.size(); ++i) {
        const CollisionEvent& a = forked.collisions[i];
        const CollisionEvent& b = serial.collisions[i];
        sameEvents = a.a == b.a && a.b == b.b && a.phase == b.phase
            && a.point == b.point && a.normal == b.normal;
    }
    check("  and hears the same collisions in the same order", sameEvents);
}

// Sleep is per contact island: a crate sleeping while the stack under it settles is
// immovable, and the stack moves out from under it. The lower box is held awake by
// hand; the upper must stay awake with it, though asleep by every measure of its own.
void testAStackSleepsTogetherOrNotAtAll() {
    std::printf("A stack settling, and what may sleep in it:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});   // floor, static

    const EntityId lower = addFallingBody(scene, {0.0f, 0.5f, 0.0f}, 0.5f);
    const EntityId upper = addFallingBody(scene, {0.0f, 1.5f, 0.0f}, 0.5f);
    // addFallingBody opts out of sleeping; this test is about sleeping.
    scene.get<Rigidbody>(lower).canSleep = true;
    scene.get<Rigidbody>(upper).canSleep = true;

    TestFrame frame(scene);
    FrameContext& ctx = frame.ctx;
    PhysicsSystem physics;

    // Watched every tick: an awake body wakes a touching sleeper by a separate rule, so
    // without islands the upper box would doze and be rewoken, awake at the end.
    constexpr float CREEP = 0.2f;
    bool upperSlept = false;

    for (int tick = 0; tick < 120; ++tick) {
        scene.get<Rigidbody>(lower).sleeping = false;
        scene.get<Rigidbody>(lower).sleepTimer = 0.0f;
        scene.get<Rigidbody>(lower).linearVelocity.x = (tick % 2 == 0) ? CREEP : -CREEP;
        physics.fixedUpdate(ctx);
        upperSlept = upperSlept || scene.get<Rigidbody>(upper).sleeping;
    }

    check("the box still creeping is awake", !scene.get<Rigidbody>(lower).sleeping);
    check("so the one resting on it never sleeps, not for a tick", !upperSlept);

    // Let go, and the island settles as one.
    for (int tick = 0; tick < 240; ++tick) physics.fixedUpdate(ctx);

    check(
        "once nothing disturbs it the stack sleeps",
        scene.get<Rigidbody>(lower).sleeping && scene.get<Rigidbody>(upper).sleeping
    );
}

// A replayed tick's rest time was counted when first lived. Counted again, a client's
// body sleeps sooner than the server's after every correction, and the solver stops
// moving it.
void testAReplayedTickDoesNotCountTowardSleep() {
    std::printf("A body at rest across a replayed tick:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId box = addFallingBody(scene, {0.0f, 0.5f, 0.0f}, 0.5f);
    scene.get<Rigidbody>(box).canSleep = true;

    TestFrame frame(scene);
    PhysicsSystem physics;
    for (int tick = 0; tick < 5; ++tick) physics.fixedUpdate(frame.ctx);

    const float lived = scene.get<Rigidbody>(box).sleepTimer;
    check("resting on the floor, it counts toward sleep", lived > 0.0f);

    for (int tick = 0; tick < 5; ++tick) {
        frame.ownedNet.beginReplayTick(InputCommand{});
        physics.fixedUpdate(frame.ctx);
        frame.ownedNet.endReplay(scene);
    }
    check("  and the same ticks replayed add nothing to it", scene.get<Rigidbody>(box).sleepTimer == lived);
    check("  nor put it to sleep", !scene.get<Rigidbody>(box).sleeping);
}

// A client does not decide a body it neither owns nor leases, so the gather hands it
// over immovable. Gravity integrated into its velocity anyway would make a remote crate
// on a decided body read as pressing down a tick of gravity, every tick - a press the
// server never applied.
void testABodyThisEndDoesNotDecideGetsNoGravity() {
    std::printf("A body this end does not decide:\n");

    // A floating box and a remote crate touching it from above; neither should move.
    Scene scene;
    const EntityId held = addFallingBody(scene, {0.0f, 0.5f, 0.0f}, 0.5f);
    scene.get<Rigidbody>(held).gravityScale = 0.0f;
    const EntityId remote = addFallingBody(scene, {0.0f, 1.499f, 0.0f}, 0.5f);

    // A client with nothing to talk to: every body is the server's but the leased one.
    NetSession client;
    check("a client session", client.connect(NetAddress{0x7F000001u, 9}, TEST_TICK_RATE));
    TestFrame frame(scene, client);
    PhysicsSystem physics;

    for (int tick = 0; tick < 60; ++tick) {
        client.lease({held});
        physics.fixedUpdate(frame.ctx);
    }
    check("the client decides the box it leased", client.simulates(held));
    check("  and not the one resting on it", !client.simulates(remote));
    check(
        "which is not driven down by a crate this end does not drop",
        std::fabs(scene.get<Transform>(held).position.y - 0.5f) < 0.005f
    );

    client.close();
}

// A tall box just past balance starts to fall slowly: its centre barely moves and it
// turns at a fraction of a radian a second. Separate linear and angular thresholds
// would read that as rest, and it would sleep leaning fifteen degrees on one edge.
void testABoxPastBalanceFallsOverRatherThanSleeping() {
    std::printf("A tall box tipped just past balance:\n");

    const glm::vec3 half(0.25f, 1.0f, 0.25f);
    const float balance = std::atan(half.x / half.y);

    for (const float past : {0.5f, 2.0f}) {
        Scene scene;
        addBox(scene, {0.0f, -0.5f, 0.0f}, {10.0f, 0.5f, 10.0f});

        // Leaning toward +X, its bottom edge on the floor at the origin.
        const glm::quat lean =
            glm::angleAxis(-(balance + glm::radians(past)), glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::vec3 centre = -(lean * glm::vec3(-half.x, -half.y, 0.0f)) + glm::vec3(0.0f, -0.001f, 0.0f);
        const EntityId box = addFallingBody(scene, centre, 0.5f);
        scene.get<Collider>(box).parts[0].halfExtents = half;
        scene.get<Transform>(box).rotation = lean;
        scene.get<Rigidbody>(box).canSleep = true;

        simulate(scene, 240);

        const glm::vec3 up = scene.get<Transform>(box).rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        const float tilt = glm::degrees(std::acos(glm::clamp(up.y, -1.0f, 1.0f)));
        std::printf(
            "      %.1f deg past balance ends tilted %.1f deg\n",
            static_cast<double>(past),
            static_cast<double>(tilt)
        );
        check(
            past < 1.0f ? "half a degree past balance, it falls over" : "two degrees past, it falls over",
            tilt > 80.0f
        );
    }
}

// Support is not always a contact. A lamp hanging still touches nothing, and a
// ragdoll's hips are held off the floor by the legs; per-body contact rest would keep
// both awake. Support is the island's: a contact or a joint to something outside it.
void testABodyHeldByAJointFallsAsleep() {
    std::printf("A lamp hanging still:\n");

    Scene scene;
    const EntityId post = addAnchor(scene, {0.0f, 5.0f, 0.0f});
    const EntityId lamp = addFallingBody(scene, {0.0f, 4.0f, 0.0f}, 0.2f);
    scene.get<Rigidbody>(lamp).canSleep = true;
    Joint joint;
    joint.connected = post;
    joint.anchor    = {0.0f, 1.0f, 0.0f};
    scene.add<Joint>(lamp, std::move(joint));

    simulate(scene, 240);
    check("it falls asleep, held by the post", scene.get<Rigidbody>(lamp).sleeping);

    // Let go of, nothing holds it, so it wakes and falls.
    scene.remove<Joint>(lamp);
    simulate(scene, 30);
    check(
        "  and wakes and falls once the joint is gone",
        !scene.get<Rigidbody>(lamp).sleeping && scene.get<Transform>(lamp).position.y < 3.9f
    );
}

// A sleeper is immovable, so what wakes it decides whether a push is a push or a wall.
// Woken only by a partner above half a metre a second, a slow shove - a character
// leaning on a crate, a platform easing in - would meet no inverse mass and move nothing.
void testASlowPushWakesASleeper() {
    std::printf("A sleeper pushed slowly:\n");

    // Two boxes on a floor, a millimetre into each other, the pusher creeping.
    auto pushed = [](bool kinematic) {
        Scene scene;
        addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
        const EntityId sleeper = addFallingBody(scene, {0.0f, 0.5f, 0.0f}, 0.5f);
        Rigidbody& rest = scene.get<Rigidbody>(sleeper);
        rest.canSleep = true;
        rest.sleeping = true;

        const EntityId pusher = addFallingBody(scene, {0.999f, 0.5f, 0.0f}, 0.5f);
        Rigidbody& push = scene.get<Rigidbody>(pusher);
        push.motion         = kinematic ? RigidbodyMotion::Kinematic : RigidbodyMotion::Dynamic;
        push.linearVelocity = {-0.3f, 0.0f, 0.0f};

        TestFrame frame(scene);
        PhysicsSystem physics;
        physics.fixedUpdate(frame.ctx);
        return !scene.get<Rigidbody>(sleeper).sleeping;
    };

    check("a dynamic body creeping into a sleeper wakes it", pushed(false));
    check("  and so does a kinematic one being driven", pushed(true));

    // The floor touches it too and must not wake it, or every sleeper in the level
    // wakes every tick.
    Scene alone;
    addBox(alone, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId resting = addFallingBody(alone, {0.0f, 0.499f, 0.0f}, 0.5f);
    alone.get<Rigidbody>(resting).canSleep = true;
    alone.get<Rigidbody>(resting).sleeping = true;
    simulate(alone, 30);
    check("a sleeper on the floor alone stays asleep", alone.get<Rigidbody>(resting).sleeping);
}

// Waking is two fields: clearing only the flag leaves a long-rest timer, and the body
// sleeps again at the end of the same tick.
void testAWokenBodyStaysAwake() {
    std::printf("A body woken by hand:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId body = addFallingBody(scene, {0.0f, 0.499f, 0.0f}, 0.5f);
    scene.get<Rigidbody>(body).canSleep = true;
    simulate(scene, 240);
    check("a box left on the floor falls asleep", scene.get<Rigidbody>(body).sleeping);

    Rigidbody::wake(scene.get<Rigidbody>(body));
    simulate(scene, 1);
    check("  and once woken is still awake after the next tick", !scene.get<Rigidbody>(body).sleeping);
}

// A sleeping island wakes as one. Woken only at the touch, a ball would wake a tower's
// top box and leave the one under it asleep - infinitely heavy for that tick - waking a
// layer a tick, each landing on a wall.
void testASleepingTowerWakesWhole() {
    std::printf("A ball landing on a sleeping tower:\n");

    constexpr int BOXES = 5;

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});   // floor, static

    std::vector<EntityId> tower;
    for (int i = 0; i < BOXES; ++i) {
        tower.push_back(addFallingBody(scene, {0.0f, 0.5f + static_cast<float>(i), 0.0f}, 0.5f));
        scene.get<Rigidbody>(tower.back()).canSleep = true;
    }

    TestFrame frame(scene);
    PhysicsSystem physics;
    for (int tick = 0; tick < 240; ++tick) physics.fixedUpdate(frame.ctx);

    const auto awake = [&] {
        int count = 0;
        for (const EntityId box : tower) count += scene.get<Rigidbody>(box).sleeping ? 0 : 1;
        return count;
    };
    check("the tower falls asleep", awake() == 0);

    // Already touching the top and coming down on it.
    const float top = scene.get<Transform>(tower.back()).position.y + 0.5f;
    const EntityId ball = addFallingBody(scene, {0.0f, top + 0.248f, 0.0f}, 0.25f);
    scene.get<Rigidbody>(ball).linearVelocity = {0.0f, -3.0f, 0.0f};
    physics.fixedUpdate(frame.ctx);

    std::printf("      %d of %d boxes awake on the tick it lands\n", awake(), BOXES);
    check("the whole tower wakes on the tick it is struck", awake() == BOXES);
}

// A sleeper's support vanishing: nothing strikes it, so no contact rule reaches it, and
// asleep it hangs in the air where the floor was.
void testASleeperWhoseSupportVanishesFalls() {
    std::printf("A sleeper whose floor is taken away:\n");

    Scene scene;
    const EntityId floor = addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId crate = addFallingBody(scene, {0.0f, 0.499f, 0.0f}, 0.5f);
    scene.get<Rigidbody>(crate).canSleep = true;
    scene.get<Rigidbody>(crate).sleeping = true;

    simulate(scene, 10);
    check("a crate asleep on the floor", scene.get<Rigidbody>(crate).sleeping);

    scene.destroyEntity(floor);
    simulate(scene, 1);
    // Gravity applies on the waking tick: it wakes while the tick decides what moves,
    // before forces are added.
    check("  wakes when the floor is gone", !scene.get<Rigidbody>(crate).sleeping);
    check("  and is falling that same tick", scene.get<Rigidbody>(crate).linearVelocity.y < -0.1f);

    simulate(scene, 59);
    check("  and falls", scene.get<Transform>(crate).position.y < -1.0f);
}

// Stacked boxes must rest *on* each other. A solver starting each tick from zero never
// quite rediscovers the stack's weight within its iterations, so the tower sinks into
// itself a centimetre at a time, leans as a face's corners converge unevenly, and
// sleeps like that - clipped through itself, unfixable by an author.
//
// The bound is per contact: a face rests within what the contact spring compresses
// under the load above, and the fifth box carries four.
void testAStackRestsOnItselfRatherThanInsideItself() {
    std::printf("Five boxes stacked, once they have settled:\n");

    constexpr int   BOXES     = 5;
    constexpr float REST_SLOP = 0.004f;   // per contact, the spring's compression

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});   // floor, static

    std::vector<EntityId> tower;
    for (int i = 0; i < BOXES; ++i) {
        tower.push_back(addFallingBody(scene, {0.0f, 0.5f + static_cast<float>(i), 0.0f}, 0.5f));
        scene.get<Rigidbody>(tower.back()).canSleep = true;
    }

    simulate(scene, 240);

    bool held = true, upright = true, inPlace = true;
    for (int i = 0; i < BOXES; ++i) {
        const Transform& t = scene.get<Transform>(tower[i]);
        const float wanted = 0.5f + static_cast<float>(i);
        const glm::vec3 up = t.rotation * glm::vec3(0.0f, 1.0f, 0.0f);

        // Every contact under this box may give by the slop, and no more.
        held    = held    && (wanted - t.position.y) < REST_SLOP * static_cast<float>(i + 1);
        upright = upright && up.y > std::cos(glm::radians(1.0f));
        inPlace = inPlace && glm::length(glm::vec2(t.position.x, t.position.z)) < 0.02f;
    }

    check("no box rests inside the one below it", held);
    check("  the tower is still standing straight", upright);
    check("  and it has not walked off its own footprint", inPlace);
    check(
        "  and it is asleep, at rest rather than settling still",
        scene.get<Rigidbody>(tower[BOXES - 1]).sleeping
    );
}

// The poses bodies were gathered at, as the contact cache reads them.
std::vector<BodyFrame> framesOf(const std::vector<PhysicsBody>& bodies) {
    std::vector<BodyFrame> frames(bodies.size());
    for (size_t i = 0; i < bodies.size(); ++i) {
        frames[i].pose.position = bodies[i].position;
        frames[i].pose.rotation = bodies[i].rotation;
    }
    return frames;
}

// Warm starting, both halves. The stack test loads contacts only along the normal, so a
// warm start dropping the friction impulse would pass it.
//
// Driven through the solver directly, since the handover is between two calls. With
// params.iterations = 0 the pre-solve and warm-start apply still run but no friction
// accumulates, so the box's travel before the relax passes (which touch velocities,
// never poses) is exactly the carry-over.
void testAContactCarriesItsFrictionIntoTheNextTick() {
    std::printf("What a contact still holds one tick later:\n");

    const std::vector<EntityId> entities = {EntityId{1, 1}, EntityId{2, 1}};

    // A dynamic box on a static floor, sliding along +X.
    auto makeBodies = [] {
        std::vector<PhysicsBody> bodies(2);
        bodies[0].invMass  = 0.0f;                       // the floor
        bodies[0].friction = 0.9f;
        bodies[1].invMass  = 1.0f;
        bodies[1].position = {0.0f, 0.5f, 0.0f};
        bodies[1].invInertiaWorld = glm::mat3(1.0f);
        bodies[1].friction = 0.9f;
        return bodies;
    };
    auto makeManifolds = [] {
        std::vector<ContactManifold> manifolds(1);
        manifolds[0].bodyA = 0;
        manifolds[0].bodyB = 1;
        manifolds[0].count = 1;
        manifolds[0].contacts[0].point  = {0.0f, 0.0f, 0.0f};
        manifolds[0].contacts[0].normal = {0.0f, 1.0f, 0.0f};
        return manifolds;
    };

    SolverParams params;
    params.dt         = 1.0f / 60.0f;
    params.iterations = 8;

    ContactCache cache;
    std::vector<JointConstraint> noJoints;

    // Tick one: nothing held yet; the friction is earned here.
    std::vector<PhysicsBody>     bodies    = makeBodies();
    std::vector<ContactManifold> manifolds = makeManifolds();
    bodies[1].linearVelocity = {2.0f, -1.0f, 0.0f};

    const std::vector<BodyFrame> gathered = framesOf(bodies);
    cache.seed(manifolds, entities, gathered);
    solveStep(bodies, manifolds, noJoints, params);
    const Contact& first = manifolds[0].contacts[0];
    const float earned1 = first.tangentImpulse1;
    const float earned2 = first.tangentImpulse2;
    check("a sliding contact accumulates friction", std::abs(earned1) + std::abs(earned2) > 1e-4f);
    cache.record(manifolds, entities, gathered);

    // Tick two: the narrowphase hands over fresh contacts, as every tick - impulses
    // zero, basis the zero vector.
    std::vector<ContactManifold> next = makeManifolds();
    check("a fresh contact starts with no basis at all", glm::length(next[0].contacts[0].tangent1) == 0.0f);

    cache.seed(next, entities, framesOf(bodies));
    const Contact& seeded = next[0].contacts[0];
    check("the seed carries the friction back as a vector", glm::length(seeded.warmFriction) > 1e-4f);
    check(
        "  and it points along the slide, in the contact plane",
        std::abs(glm::dot(glm::normalize(seeded.warmFriction), glm::vec3(0.0f, 1.0f, 0.0f))) < 1e-4f
    );

    // No passes: the box moves sideways only by the carry-over resolved onto the new basis.
    SolverParams carryOnly = params;
    carryOnly.iterations = 0;
    std::vector<PhysicsBody> fresh = makeBodies();
    solveStep(fresh, next, noJoints, carryOnly);

    const float sideways =
        glm::length(glm::vec2(fresh[1].position.x, fresh[1].position.z)) / params.dt;
    check("the solve resolves it onto the basis it just built", sideways > 1e-4f);
    check(
        "  and the vector it resolved is the one that was earned",
        nearly(sideways, std::sqrt(earned1 * earned1 + earned2 * earned2))
    );
}

// A sleeping stack keeps its contacts, and the cache carries what they held. Neither
// body can move, so a biased pass over the pair, with no mass to push, only leaks the
// accumulated impulse by the spring's leak - tick after tick, until a stack woken
// minutes later starts from nothing and sags.
void testASleepingPairKeepsWhatItHeld() {
    std::printf("What a sleeping pair holds, tick after tick:\n");

    const std::vector<EntityId> entities = {EntityId{1, 1}, EntityId{2, 1}};

    // A box asleep on a static floor: both immovable to the solve.
    std::vector<PhysicsBody> bodies(2);
    bodies[1].position = {0.0f, 0.5f, 0.0f};

    SolverParams params;
    params.dt         = 1.0f / 60.0f;
    params.iterations = 8;

    ContactCache cache;
    std::vector<JointConstraint> noJoints;

    std::vector<ContactManifold> held(1);
    held[0].bodyA = 0;
    held[0].bodyB = 1;
    held[0].count = 1;
    held[0].contacts[0].normal        = {0.0f, 1.0f, 0.0f};
    held[0].contacts[0].normalImpulse   = 0.8f;
    held[0].contacts[0].tangent1        = {1.0f, 0.0f, 0.0f};
    held[0].contacts[0].tangentImpulse1 = 0.1f;
    const std::vector<BodyFrame> frames = framesOf(bodies);
    cache.record(held, entities, frames);

    std::vector<ContactManifold> manifolds;
    for (int tick = 0; tick < 60; ++tick) {
        // As the narrowphase hands it over: nothing but where and which way.
        manifolds.assign(1, ContactManifold{});
        manifolds[0].bodyB = 1;
        manifolds[0].count = 1;
        manifolds[0].contacts[0].normal = {0.0f, 1.0f, 0.0f};

        cache.seed(manifolds, entities, frames);
        solveStep(bodies, manifolds, noJoints, params);
        cache.record(manifolds, entities, frames);
    }

    const Contact& contact = manifolds[0].contacts[0];
    const glm::vec3 friction = contact.tangentImpulse1 * contact.tangent1
        + contact.tangentImpulse2 * contact.tangent2;
    check("a second of sleep leaves the normal impulse whole", nearly(contact.normalImpulse, 0.8f));
    check("  and the friction", nearly(friction.x, 0.1f) && nearly(glm::length(friction), 0.1f));
}

// The same for a joint: every link of a sleeping chain is immovable, and preparation
// zeroing each joint's load before finding that out would erase it on the first tick
// of sleep - a lamp on a chain would wake hanging as if from nothing.
void testASleepingJointKeepsWhatItHeld() {
    std::printf("What a sleeping joint holds, tick after tick:\n");

    // A link asleep under a post: both immovable to the solve.
    std::vector<PhysicsBody> bodies(2);
    bodies[1].position = {0.0f, -1.0f, 0.0f};

    SolverParams params;
    params.dt         = 1.0f / 60.0f;
    params.iterations = 8;

    std::vector<ContactManifold> noContacts;

    for (const float distance : {-1.0f, 1.0f}) {
        const glm::vec3 load = {0.0f, 4.9f, 0.0f};
        glm::vec3 held = load;
        for (int tick = 0; tick < 60; ++tick) {
            // As the gather hands it over: where, and what it held last tick.
            std::vector<JointConstraint> joints(1);
            joints[0].bodyA    = 1;
            joints[0].bodyB    = 0;
            joints[0].anchorA  = {0.0f, 0.5f, 0.0f};
            // Coincident for the point joint, the length apart for the other.
            joints[0].anchorB  = {0.0f, distance < 0.0f ? -0.5f : 0.5f, 0.0f};
            joints[0].distance = distance;
            joints[0].impulse  = held;

            solveStep(bodies, noContacts, joints, params);
            held = joints[0].impulse;
        }
        const char* label = distance < 0.0f
            ? "a second of sleep leaves a point joint's impulse whole"
            : "  and a distance joint's";
        check(label, nearly(held.y, load.y) && nearly(glm::length(held), glm::length(load)));
    }
}

// The cache stores each point relative to its bodies so a pair riding a platform
// matches itself next tick. Taken after integration, the point would be off by the
// travel, and beyond a centimetre (0.6 m/s at 60 Hz) every contact on anything moving
// would start from nothing.
void testAContactOnAMovingPlatformIsCarriedIntoTheNextTick() {
    std::printf("A contact on a moving platform, one tick later:\n");

    const std::vector<EntityId> entities = {EntityId{1, 1}, EntityId{2, 1}};

    // An immovable platform carrying a box, both at 3 m/s along +X: 5 cm a tick.
    std::vector<PhysicsBody> bodies(2);
    bodies[0].invMass        = 0.0f;
    bodies[0].linearVelocity = {3.0f, 0.0f, 0.0f};
    bodies[1].invMass         = 1.0f;
    bodies[1].position        = {0.0f, 0.5f, 0.0f};
    bodies[1].invInertiaWorld = glm::mat3(1.0f);
    bodies[1].linearVelocity  = {3.0f, -1.0f, 0.0f};

    auto contactAt = [](const glm::vec3& point) {
        std::vector<ContactManifold> manifolds(1);
        manifolds[0].bodyA = 0;
        manifolds[0].bodyB = 1;
        manifolds[0].count = 1;
        manifolds[0].contacts[0].point  = point;
        manifolds[0].contacts[0].normal = {0.0f, 1.0f, 0.0f};
        return manifolds;
    };

    SolverParams params;
    params.dt         = 1.0f / 60.0f;
    params.iterations = 8;

    ContactCache cache;
    std::vector<JointConstraint> noJoints;
    std::vector<ContactManifold> manifolds = contactAt({0.0f, 0.0f, 0.0f});
    const std::vector<BodyFrame> gathered = framesOf(bodies);
    cache.seed(manifolds, entities, gathered);
    solveStep(bodies, manifolds, noJoints, params);
    const float held = manifolds[0].contacts[0].normalImpulse;
    check("the box presses on the platform", held > 0.0f);

    // The solve moved the box; the platform is the project's to drive, and it moves
    // before the record too.
    bodies[0].position += bodies[0].linearVelocity * params.dt;
    const float travelled = bodies[0].position.x;
    check("  and the platform moves further than the match radius", travelled > 0.01f);
    cache.record(manifolds, entities, gathered);

    // Next tick the narrowphase finds the contact where the platform took it.
    std::vector<ContactManifold> next = contactAt({travelled, 0.0f, 0.0f});
    cache.seed(next, entities, framesOf(bodies));
    check("the next tick starts from the impulse it held", nearly(next[0].contacts[0].normalImpulse, held));

    // A turning platform, a metre from the axis at 0.2 rad a tick: 20 cm of travel,
    // survived only if the offset turned with the platform.
    std::vector<PhysicsBody> turning = bodies;
    turning[0].position = glm::vec3(0.0f);
    turning[0].rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    std::vector<ContactManifold> before = contactAt({1.0f, 0.0f, 0.0f});
    before[0].contacts[0].normalImpulse = held;
    before[0].contacts[0].rA            = {1.0f, 0.0f, 0.0f};   // as the solve leaves it
    ContactCache spun;
    spun.record(before, entities, framesOf(turning));

    const glm::quat turn = glm::angleAxis(0.2f, glm::vec3(0.0f, 1.0f, 0.0f));
    turning[0].rotation = turn;
    std::vector<ContactManifold> after = contactAt(turn * glm::vec3(1.0f, 0.0f, 0.0f));
    spun.seed(after, entities, framesOf(turning));
    check(
        "a contact on a turning platform is carried as well",
        nearly(after[0].contacts[0].normalImpulse, held)
    );
}

// A contact is the same if it stayed put on either body. Matched only in A's frame (the
// lower slot), a box sliding over an older floor would lose its warm start above a
// centimetre a tick, 0.6 m/s at 60 Hz, while on a newer floor it kept it at any speed.
void testASlidingContactIsCarriedWhicheverBodyCameFirst() {
    std::printf("A box sliding over a floor, one tick later:\n");

    for (const bool floorFirst : {true, false}) {
        const uint32_t box = floorFirst ? 1 : 0;
        const std::vector<EntityId> entities = {EntityId{1, 1}, EntityId{2, 1}};

        std::vector<PhysicsBody> bodies(2);
        bodies[box].invMass         = 1.0f;
        bodies[box].position        = {0.0f, 0.5f, 0.0f};
        bodies[box].invInertiaWorld = glm::mat3(1.0f);

        // A corner of the box on the floor, as the solve leaves it, normal A -> B.
        const auto cornerAt = [&](const glm::vec3& point) {
            std::vector<ContactManifold> manifolds(1);
            manifolds[0].bodyA = 0;
            manifolds[0].bodyB = 1;
            manifolds[0].count = 1;
            Contact& contact = manifolds[0].contacts[0];
            contact.point  = point;
            contact.normal = floorFirst ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, -1.0f, 0.0f);
            contact.rA     = point - bodies[0].position;
            contact.rB     = point - bodies[1].position;
            return manifolds;
        };

        std::vector<ContactManifold> held = cornerAt({0.5f, 0.0f, 0.5f});
        held[0].contacts[0].normalImpulse = 0.8f;
        ContactCache cache;
        cache.record(held, entities, framesOf(bodies));

        // Five centimetres along the floor at 3 m/s; the corner goes with the box.
        bodies[box].position.x += 0.05f;
        std::vector<ContactManifold> next = cornerAt({0.55f, 0.0f, 0.5f});
        cache.seed(next, entities, framesOf(bodies));
        check(
            floorFirst ? "the floor made first, the box keeps what it held" : "  and the box made first",
            nearly(next[0].contacts[0].normalImpulse, 0.8f)
        );
    }
}

// A contact's mass is 1 / k, k an inverse mass. Held against glm::epsilon to tell "both
// immovable", a 20,000-tonne body (inverse mass below that epsilon) would be a wall:
// unpushable, pushing nothing back.
void testAHeavyBodyStillHasAnEffectiveMass() {
    std::printf("The effective mass of a very heavy body:\n");

    constexpr float TONNES = 2.0e7f;   // kilograms
    PhysicsBody floor;                 // immovable
    PhysicsBody heavy;
    heavy.invMass = 1.0f / TONNES;

    const float mass = SolverMath::effectiveMass(
        floor,
        heavy,
        glm::vec3(0.0f),
        glm::vec3(0.0f),
        {0.0f, 1.0f, 0.0f}
    );
    check("a twenty-thousand-tonne body has an effective mass", mass > 0.0f);
    check("  and it is that body's", std::fabs(mass - TONNES) < TONNES * 1e-4f);
    check(
        "two immovable bodies still have none",
        SolverMath::effectiveMass(floor, floor, glm::vec3(1.0f), glm::vec3(1.0f), {0.0f, 1.0f, 0.0f}) == 0.0f
    );
}

// The contact cache is keyed on entity slots, which a replacement world reuses. The
// second world's first contacts must not inherit the first's impulses, or a scene
// plays differently depending on what was loaded before.
void testAReplacedWorldStartsFromNothing() {
    std::printf("A physics system that has run another world:\n");

    // Resting long enough for the cache to hold its weight.
    auto build = [](Scene& scene, float y) {
        addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
        return addFallingBody(scene, {0.0f, y, 0.0f}, 0.5f);
    };

    Scene scene;
    build(scene, 0.5f);
    TestFrame frame(scene);
    PhysicsSystem reused;
    for (int tick = 0; tick < 60; ++tick) reused.fixedUpdate(frame.ctx);

    // Replaced as a load does - built aside, swapped in - by a world whose box sits a few
    // millimetres off, well inside the radius a remembered contact matches within.
    Scene staging;
    const EntityId box = build(staging, 0.497f);
    scene.swap(staging);
    reused.fixedUpdate(frame.ctx);

    Scene fresh;
    const EntityId same = build(fresh, 0.497f);
    TestFrame freshFrame(fresh);
    PhysicsSystem neverRan;
    neverRan.fixedUpdate(freshFrame.ctx);

    check("the replacement reuses the slots", box.slot() == same.slot());
    check(
        "its first tick is the first tick of a new system",
        scene.get<Transform>(box).position == fresh.get<Transform>(same).position
            && scene.get<Rigidbody>(box).linearVelocity == fresh.get<Rigidbody>(same).linearVelocity
    );
}

// The same tower, turned. In testAStackRestsOnItselfRatherThanInsideItself every box
// is axis-aligned with the one below, the one case where the first four clipped
// vertices happen to be a square. Yawed, the clip returns an octagon, and four
// consecutive vertices are an arc off to one side: the manifold centres near a corner
// and the tower leans and walks. Box on box is the contact the solver sees most.
void testAYawedStackRestsAsSquarelyAsAnAlignedOne() {
    std::printf("Five boxes stacked, each turned against the one below it:\n");

    constexpr int   BOXES     = 5;
    constexpr float STEP_DEG  = 45.0f;   // the worst case: the octagon is widest here
    constexpr float MAX_DRIFT = 0.02f;   // the aligned tower's own bound
    constexpr float MAX_TILT  = 1.0f;    // degrees, as testAStackRests... allows

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});

    std::vector<EntityId> tower;
    for (int i = 0; i < BOXES; ++i) {
        const EntityId box =
            addFallingBody(scene, {0.0f, 0.5f + static_cast<float>(i), 0.0f}, 0.5f);
        scene.get<Transform>(box).rotation = glm::angleAxis(
            glm::radians(STEP_DEG * static_cast<float>(i)),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        scene.get<Rigidbody>(box).canSleep = true;
        tower.push_back(box);
    }

    simulate(scene, 240);

    float worstDrift = 0.0f;
    float worstTilt  = 0.0f;
    for (const EntityId box : tower) {
        const Transform& t = scene.get<Transform>(box);
        const glm::vec3  up = t.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        worstDrift = std::max(worstDrift, glm::length(glm::vec2(t.position.x, t.position.z)));
        worstTilt  = std::max(worstTilt, glm::degrees(std::acos(glm::clamp(up.y, -1.0f, 1.0f))));
    }

    std::printf("      worst drift %.4f m, worst tilt %.3f deg\n", worstDrift, worstTilt);
    check("no box has walked off its own footprint", worstDrift < MAX_DRIFT);
    check("  and none of them is leaning",           worstTilt  < MAX_TILT);
}

// A kinematic body has no inverse mass, and the pose writeback skips every such body. One
// asking only for Static would integrate its orientation but not its position - half
// velocity-driven, half Transform-driven - and integrateForces skips it, so the spin
// would never damp.
void testAKinematicBodyIsMovedByItsTransformAndNotByItsVelocity() {
    std::printf("A velocity set on a body the solver cannot move:\n");

    Scene scene;
    addBox(scene, {0.0f, -20.0f, 0.0f}, {8.0f, 0.5f, 8.0f});   // far below; nothing lands

    auto withVelocity = [&](bool kinematic) {
        const EntityId id = addFallingBody(scene, {0.0f, 0.0f, 0.0f}, 0.5f);
        Rigidbody& rb = scene.get<Rigidbody>(id);
        rb.motion          = kinematic ? RigidbodyMotion::Kinematic : RigidbodyMotion::Dynamic;
        rb.gravityScale    = 0.0f;                       // gravity is not what is under test
        rb.linearVelocity  = {1.0f, 0.0f, 0.0f};
        rb.angularVelocity = {0.0f, 1.0f, 0.0f};
        return id;
    };

    const EntityId driven = withVelocity(true);
    const EntityId free   = withVelocity(false);

    simulate(scene, 60);

    const Transform& k = scene.get<Transform>(driven);
    const float turned = 2.0f * std::acos(glm::clamp(std::fabs(k.rotation.w), -1.0f, 1.0f));
    check("a kinematic body does not travel on its linear velocity", nearly(k.position.x, 0.0f));
    check("  and does not turn on its angular velocity either", turned < 0.01f);

    // The control: the solver still moves what it can, both ways.
    const Transform& d = scene.get<Transform>(free);
    const float spun = 2.0f * std::acos(glm::clamp(std::fabs(d.rotation.w), -1.0f, 1.0f));
    check("a dynamic body travels", d.position.x > 0.5f);
    check("  and turns", spun > 0.5f);

    // The velocity is still the project's to state: a contact reads it, which is how a
    // kinematic platform pushes what stands on it.
    check(
        "and the velocity it was given is still on the body",
        nearly(scene.get<Rigidbody>(driven).linearVelocity.x, 1.0f)
    );
}

// Mass is a dynamic body's alone, and must be positive. One without is a mistake the
// author is told about once, not a second way of saying Static.
void testADynamicBodyWithNoMassIsNamedAndHeldStill() {
    std::printf("A dynamic body authored with no mass:\n");

    Scene scene;
    const EntityId slab = addFallingBody(scene, {0.0f, 5.0f, 0.0f}, 0.5f);
    scene.get<Rigidbody>(slab).mass = 0.0f;

    EngineErrorLog errors;
    setErrorSink(&errors);
    simulate(scene, 10);
    setErrorSink(nullptr);

    check("it does not fall", nearly(scene.get<Transform>(slab).position.y, 5.0f));
    check(
        "  and it is named, once",
        errors.entries().size() == 1 && errors.entries()[0].repeatCount == 1
            && errors.entries()[0].category == "Physics"
    );
    check("  but it is no static body to the wire", !isStaticBody(scene, slab));
}

} // namespace

void runPhysicsSolverTests() {
    testAStackSleepsTogetherOrNotAtAll();
    testABodyThisEndDoesNotDecideGetsNoGravity();
    testAReplayedTickDoesNotCountTowardSleep();
    testABoxPastBalanceFallsOverRatherThanSleeping();
    testABodyHeldByAJointFallsAsleep();
    testASlowPushWakesASleeper();
    testAWokenBodyStaysAwake();
    testASleepingTowerWakesWhole();
    testASleeperWhoseSupportVanishesFalls();
    testAStackRestsOnItselfRatherThanInsideItself();
    testAContactCarriesItsFrictionIntoTheNextTick();
    testASleepingPairKeepsWhatItHeld();
    testASleepingJointKeepsWhatItHeld();
    testAContactOnAMovingPlatformIsCarriedIntoTheNextTick();
    testASlidingContactIsCarriedWhicheverBodyCameFirst();
    testAHeavyBodyStillHasAnEffectiveMass();
    testAReplacedWorldStartsFromNothing();
    testAKinematicBodyIsMovedByItsTransformAndNotByItsVelocity();
    testADynamicBodyWithNoMassIsNamedAndHeldStill();
    testAYawedStackRestsAsSquarelyAsAnAlignedOne();
    testTheSameWorldSimulatesTheSameWay();
    testTheSameWorldRunTwiceEndsBitIdentical();
    testABusyPileStepsTheSameOnAnyThread();
}
