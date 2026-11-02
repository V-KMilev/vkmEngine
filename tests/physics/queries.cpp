// What gameplay asks of the world without moving it: rays, swept spheres, overlaps.

#include <vector>

#include "vkm_physics/sweep.h"

#include "io/asset/generate/mesh_generators.h"
#include "ecs/component/physics/mesh_collider.h"
#include "system/physics/query.h"

#include "harness/check.h"
#include "harness/world.h"
#include "physics/bodies.h"

using namespace Vkm::Test;

TEST("a ray finds the nearest body it strikes, where and facing which way") {
    Frame frame;
    Scene& scene = frame.scene;
    // The far one in the lower slot, so offered first: the nearest wins, not the first found.
    addBody(scene, {9.0f, 0.0f, 0.0f}, box(glm::vec3(0.5f)), RigidbodyMotion::Static);
    const EntityId near = addBody(scene, {5.0f, 0.0f, 0.0f}, box(glm::vec3(0.5f)), RigidbodyMotion::Static);

    RayHit hit;
    REQUIRE(raycast(scene, {0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, 100.0f, hit));
    CHECK(hit.entity == near);
    CHECK_NEAR(hit.distance, 4.5f, 1e-4f);
    CHECK_NEAR(hit.point, glm::vec3(4.5f, 0.0f, 0.0f), 1e-4f);
    CHECK_NEAR(hit.normal, glm::vec3(-1.0f, 0.0f, 0.0f), 1e-4f);

    RayHit untouched;
    untouched.distance = -7.0f;
    CHECK(!raycast(scene, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 4.4f, untouched));
    CHECK(untouched.distance == -7.0f);
    CHECK(!raycast(scene, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.0f, untouched));
}

TEST("a ray's filter decides what it sees: the caster, triggers, static or dynamic, layers") {
    Frame frame;
    Scene& scene = frame.scene;
    const EntityId self    = addBody(scene, {0.0f, 0.0f, 0.0f}, capsule(0.5f, 0.5f));
    const EntityId trigger = addBody(
        scene,
        {2.0f, 0.0f, 0.0f},
        box(glm::vec3(0.2f)),
        RigidbodyMotion::Static
    );
    scene.get<Collider>(trigger).isTrigger = true;
    const EntityId moving  = addBody(scene, {4.0f, 0.0f, 0.0f}, box(glm::vec3(0.2f)));
    const EntityId layered = addBody(
        scene,
        {6.0f, 0.0f, 0.0f},
        box(glm::vec3(0.2f)),
        RigidbodyMotion::Static
    );
    frame.scene.get<Rigidbody>(layered).layer = 1 << 5;
    const EntityId wall = addBody(scene, {8.0f, 0.0f, 0.0f}, box(glm::vec3(0.2f)), RigidbodyMotion::Static);
    // A collider with no body is in no query.
    const EntityId bare = scene.createEntity();
    Transform at;
    at.position = {1.0f, 0.0f, 0.0f};
    scene.add(bare, at);
    scene.add(bare, Collider{});

    auto first = [&](const QueryFilter& filter) {
        RayHit hit;
        return raycast(
            scene,
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            50.0f,
            hit,
            filter
        ) ? hit.entity : EntityId{};
    };
    QueryFilter filter;
    CHECK(first(filter) == self);  // from inside, at distance zero
    filter.ignore = self;
    CHECK(first(filter) == moving);
    filter.hitTriggers = true;
    CHECK(first(filter) == trigger);
    filter.hitTriggers = false;
    filter.hitDynamic  = false;
    CHECK(first(filter) == layered);
    filter.layerMask = ~(1 << 5);
    CHECK(first(filter) == wall);
    filter.hitStatic = false;
    CHECK(!first(filter));
    (void)bare;
}

TEST("a ray from inside a body hits it at distance zero, and ties go to the lower slot") {
    Frame frame;
    Scene& scene = frame.scene;
    const EntityId around = addBody(scene, {0.0f, 0.0f, 0.0f}, box(glm::vec3(1.0f)), RigidbodyMotion::Static);
    const EntityId first  = addBody(scene, {0.0f, 5.0f, 0.0f}, box(glm::vec3(0.5f)), RigidbodyMotion::Static);
    const EntityId twin   = addBody(scene, {0.0f, 5.0f, 0.0f}, box(glm::vec3(0.5f)), RigidbodyMotion::Static);

    RayHit hit;
    REQUIRE(raycast(scene, {0.2f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 10.0f, hit));
    CHECK(hit.entity == around);
    CHECK(hit.distance == 0.0f);

    QueryFilter past;
    past.ignore = around;
    REQUIRE(raycast(scene, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 10.0f, hit, past));
    CHECK(hit.entity == first);
    (void)twin;
}

TEST("a swept sphere stops at its first touch") {
    Frame frame;
    Scene& scene = frame.scene;
    const EntityId wall = addBody(
        scene,
        {5.0f, 0.0f, 0.0f},
        box({0.5f, 2.0f, 2.0f}),
        RigidbodyMotion::Static
    );
    // Off the ray's line, inside the sphere's reach: only a sphere meets it first.
    const EntityId post = addBody(scene, {3.0f, 0.7f, 0.0f}, box(glm::vec3(0.2f)), RigidbodyMotion::Static);

    RayHit hit;
    REQUIRE(spherecast(scene, {0.0f, 0.0f, 0.0f}, 0.6f, {1.0f, 0.0f, 0.0f}, 20.0f, hit));
    CHECK(hit.entity == post);

    scene.destroyEntity(post);
    REQUIRE(spherecast(scene, {0.0f, 0.0f, 0.0f}, 0.6f, {1.0f, 0.0f, 0.0f}, 20.0f, hit));
    CHECK(hit.entity == wall);
    CHECK_NEAR(hit.distance, 3.9f, 1e-3f);
    CHECK_NEAR(hit.normal, glm::vec3(-1.0f, 0.0f, 0.0f), 1e-3f);
    CHECK(!spherecast(scene, {0.0f, 0.0f, 0.0f}, 0.6f, {1.0f, 0.0f, 0.0f}, 3.8f, hit));
}

TEST("an overlap finds each body it reaches once, lowest slot first") {
    Frame frame;
    Scene& scene = frame.scene;
    std::vector<EntityId> inside;
    for (int i = 0; i < 3; ++i) inside.push_back(
        addBody(scene, {i * 1.0f, 0.0f, 0.0f}, box(glm::vec3(0.3f)))
    );
    addBody(scene, {6.0f, 0.0f, 0.0f}, box(glm::vec3(0.3f)));
    // Two parts both inside: still one entry.
    scene.get<Collider>(inside[1]).parts.push_back(box(glm::vec3(0.1f), {0.0f, 0.2f, 0.0f}));

    std::vector<EntityId> found;
    overlapSphere(scene, {1.0f, 0.0f, 0.0f}, 1.5f, found);
    CHECK(found == inside);
    overlapSphere(scene, {1.0f, 0.0f, 0.0f}, 0.0f, found);
    CHECK(found.empty());
}

TEST("many rays at one moment through QueryParts answer as raycast does") {
    Frame frame;
    Scene& scene = frame.scene;
    for (int i = 0; i < 40; ++i) {
        const glm::vec3 at(std::sin(i * 2.1f) * 6.0f, std::cos(i * 1.3f) * 3.0f, std::sin(i * 0.7f) * 6.0f);
        const EntityId body = addBody(
            scene,
            at,
            i % 2 ? box(glm::vec3(0.4f, 0.6f, 0.3f)) : capsule(0.3f, 0.4f),
            RigidbodyMotion::Static,
            glm::angleAxis(i * 0.5f, glm::normalize(glm::vec3(0.3f, 1.0f, i % 4)))
        );
        // Every third of two parts: each part answers, not the first alone.
        if (i % 3 == 0) scene.get<Collider>(body).parts.push_back(box(glm::vec3(0.3f), {0.0f, 0.9f, 0.0f}));
    }
    QueryParts parts;
    parts.gather(scene, QueryFilter{}, Math::AABB{glm::vec3(-20.0f), glm::vec3(20.0f)});

    int hits = 0;
    for (int i = 0; i < 200; ++i) {
        const glm::vec3 origin(0.0f, 0.0f, 0.0f);
        const glm::vec3 direction(std::sin(i * 0.37f), std::sin(i * 0.11f) * 0.5f, std::cos(i * 0.37f));
        RayHit one, many;
        const bool alone = raycast(scene, origin, direction, 15.0f, one);
        const bool batch = parts.raycast(origin, direction, 15.0f, many);
        CHECK(alone == batch);
        if (!alone || !batch) continue;
        ++hits;
        CHECK(one.entity == many.entity);
        CHECK_NEAR(one.distance, many.distance, 1e-5f);
        CHECK_NEAR(one.normal, many.normal, 1e-5f);
    }
    CHECK(hits > 20);
}

TEST("a part answers a ray and a sphere: the distance, the outward normal, zero from inside, none past the limit") {
    const glm::mat3 level(1.0f);
    float t = -1.0f;
    glm::vec3 normal(0.0f);

    using Vkm::Physics::sweepPart;
    const Vkm::Physics::Part cube = physicsPartOf(box(glm::vec3(1.0f)));
    CHECK(
        sweepPart(
            cube,
            nullptr,
            {0.0f, 0.0f, 0.0f},
            level,
            {-5.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            0.0f,
            10.0f,
            t,
            normal
        )
    );
    CHECK_NEAR(t, 4.0f, 1e-4f);
    CHECK_NEAR(normal, glm::vec3(-1.0f, 0.0f, 0.0f), 1e-4f);
    CHECK(
        sweepPart(
            cube,
            nullptr,
            {0.0f, 0.0f, 0.0f},
            level,
            {-5.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            0.5f,
            10.0f,
            t,
            normal
        )
    );
    CHECK_NEAR(t, 3.5f, 1e-4f);
    CHECK(sweepPart(
        cube,
        nullptr,
        {0.0f, 0.0f, 0.0f},
        level,
        {0.2f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        0.0f,
        10.0f,
        t,
        normal
    ));
    CHECK(t == 0.0f);
    CHECK(!sweepPart(
        cube,
        nullptr,
        {0.0f, 0.0f, 0.0f},
        level,
        {-5.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        0.0f,
        3.9f,
        t,
        normal
    ));
    // A rotated box: its face turns with it.
    const glm::mat3 turned = glm::mat3_cast(glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
    CHECK(
        sweepPart(
            cube,
            nullptr,
            {0.0f, 0.0f, 0.0f},
            turned,
            {-5.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            0.0f,
            10.0f,
            t,
            normal
        )
    );
    CHECK_NEAR(t, 5.0f - std::sqrt(2.0f), 1e-4f);

    const Vkm::Physics::Part pill = physicsPartOf(capsule(0.5f, 1.0f));
    CHECK(
        sweepPart(
            pill,
            nullptr,
            {0.0f, 0.0f, 0.0f},
            level,
            {0.0f, 5.0f, 0.0f},
            {0.0f, -1.0f, 0.0f},
            0.0f,
            10.0f,
            t,
            normal
        )
    );
    CHECK_NEAR(t, 3.5f, 1e-4f);
    CHECK_NEAR(normal, glm::vec3(0.0f, 1.0f, 0.0f), 1e-4f);
    CHECK(
        sweepPart(
            pill,
            nullptr,
            {0.0f, 0.0f, 0.0f},
            level,
            {-5.0f, 0.5f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            0.25f,
            10.0f,
            t,
            normal
        )
    );
    CHECK_NEAR(t, 4.25f, 1e-4f);
    CHECK_NEAR(normal, glm::vec3(-1.0f, 0.0f, 0.0f), 1e-4f);
}

TEST("a ray meets a mesh collider's triangles, and passes through a gap in them") {
    Frame frame;
    Scene& scene = frame.scene;
    const MeshHandle ring = addGeneratedMesh(frame.resources, generateRing({1.0f, 2.0f, 32}));
    const EntityId id = scene.createEntity();
    scene.add(id, Transform{});
    Rigidbody body;
    body.motion = RigidbodyMotion::Static;
    scene.add(id, body);
    Collider collider;
    ColliderPart part;
    part.shape     = ColliderShape::Mesh;
    part.mesh      = ring;
    collider.parts = {part};
    REQUIRE(syncMeshCollider(collider, frame.resources));
    scene.add(id, collider);

    RayHit hit;
    REQUIRE(raycast(scene, {1.5f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, hit));
    CHECK(hit.entity == id);
    CHECK_NEAR(hit.distance, 3.0f, 1e-4f);
    CHECK(!raycast(scene, {0.0f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, hit));
    CHECK(!raycast(scene, {2.5f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, hit));
}
