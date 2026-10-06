#include "physics/physics_support.h"

#include "system/physics/query/query.h"

namespace {

// A unit box at +5 on X and a capsule at -5: a cast either way meets one square on.
void testRaycastShapes() {
    std::printf("Raycast - shapes:\n");

    Scene scene;
    const EntityId box = addBody(scene, {5.0f, 0.0f, 0.0f}, ColliderShape::Box);
    const EntityId capsule =
        addBody(scene, {-5.0f, 0.0f, 0.0f}, ColliderShape::Capsule);
    RayHit hit;

    check("a box ahead is hit", raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit));
    check("  the box entity is reported", hit.entity == box);
    check("  at its near face, not its centre", nearly(hit.distance, 4.5f));
    check("  with the normal facing the ray", nearly(hit.normal.x, -1.0f));
    check("  and the point on that face", nearly(hit.point.x, 4.5f));

    check("a capsule behind is hit when cast at", raycast(scene, {0,0,0}, {-1,0,0}, 100.0f, hit));
    check("  the capsule entity is reported", hit.entity == capsule);
    check("  at radius off its axis", nearly(hit.distance, 4.5f));

    // The capsule's segment is +-0.5 on Y, swept by 0.5, so it spans [-1, 1].
    check("its cap is hit from above", raycast(scene, {-5.0f, 5.0f, 0.0f}, {0,-1,0}, 100.0f, hit));
    check("  at the top of the sweep", nearly(hit.distance, 4.0f));
    check("a ray outside the cap radius misses", !raycast(scene, {-5.0f, 5.0f, 0.9f}, {0,-1,0}, 100.0f, hit));

    // Turned 45 degrees about Y, a box presents an edge, nearer than the flat face by
    // exactly the difference between a half extent and its diagonal.
    Scene turned;
    const glm::quat spin = glm::angleAxis(glm::radians(45.0f), glm::vec3(0,1,0));
    addBody(turned, {5.0f, 0.0f, 0.0f}, ColliderShape::Box, spin);
    check("a rotated box is hit", raycast(turned, {0,0,0}, {1,0,0}, 100.0f, hit));
    check("  on its edge rather than its axis-aligned face", nearly(hit.distance, 5.0f - std::sqrt(0.5f)));
}

// When a caller can trust a false: a ray pointed away, one stopping short, one inside.
void testRaycastMisses() {
    std::printf("Raycast - misses and degenerate input:\n");

    Scene scene;
    addBody(scene, { 5.0f, 0.0f, 0.0f}, ColliderShape::Box);
    addBody(scene, {-5.0f, 0.0f, 0.0f}, ColliderShape::Capsule);
    RayHit hit;

    check("a ray stopping short of the box misses", !raycast(scene, {0,0,0}, {1,0,0}, 4.0f, hit));
    check("a ray passing over everything misses", !raycast(scene, {0,10,0}, {1,0,0}, 100.0f, hit));

    // The capsule sits behind the ray. Its side wall solves to a negative distance;
    // read as a hit, it puts a phantom body at the caster's feet that wins every query.
    check(
        "a body behind the origin is not a hit at zero",
        raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit) && hit.distance > 1.0f
    );

    check("a zero direction finds nothing", !raycast(scene, {0,0,0}, {0,0,0}, 100.0f, hit));
    check("a negative distance finds nothing", !raycast(scene, {0,0,0}, {1,0,0}, -1.0f, hit));

    // Directions built from velocity * timestep are small for slow bodies; rejecting
    // them as degenerate would be a silent wrong answer.
    check(
        "half a millimetre of direction still casts",
        raycast(scene, {0,0,0}, {0.0005f, 0.0f, 0.0f}, 100.0f, hit)
    );
    check("  and normalizes to the same answer", nearly(hit.distance, 4.5f));

    check(
        "a ray starting inside a body reports it",
        raycast(scene, {5.0f, 0.0f, 0.0f}, {1,0,0}, 100.0f, hit)
    );
    check("  at distance zero", nearly(hit.distance, 0.0f));
    check("  with a normal facing the caster", nearly(hit.normal.x, -1.0f));
}

void testRaycastFilters() {
    std::printf("Raycast - filters and ordering:\n");

    Scene scene;
    const EntityId farBox = addBody(scene, {5.0f, 0.0f, 0.0f}, ColliderShape::Box);
    const EntityId near_ = addBody(scene, {2.0f, 0.0f, 0.0f}, ColliderShape::Box);
    RayHit hit;

    check("two bodies in line resolve to one hit", raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit));
    check("  the nearer one wins", hit.entity == near_);
    check("  at its distance", nearly(hit.distance, 1.5f));

    QueryFilter skipNear;
    skipNear.ignore = near_;
    check(
        "ignore skips that entity",
        raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit, skipNear) && hit.entity == farBox
    );

    QueryFilter dynamicOnly;
    dynamicOnly.hitStatic = false;
    check(
        "hitStatic false skips immovable bodies",
        !raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit, dynamicOnly)
    );

    scene.get<Collider>(near_).isTrigger = true;
    check(
        "a trigger is passed through by default",
        raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit) && hit.entity == farBox
    );
    QueryFilter withTriggers;
    withTriggers.hitTriggers = true;
    check(
        "hitTriggers sees it",
        raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit, withTriggers) && hit.entity == near_
    );
    scene.get<Collider>(near_).isTrigger = false;

    scene.get<Collider>(near_).enabled = false;
    check(
        "a disabled collider is not in the query",
        raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit) && hit.entity == farBox
    );
}

// Two bodies at the same distance - coincident hitboxes, or a cast starting inside
// both - tie, and the lower slot wins. Storage order reflects build history, so a
// tie settled by it could name different bodies on server and client replaying a
// tick, and step-up reads the named body's normal.
void testAQueryTieGoesToTheLowerSlot() {
    std::printf("Two bodies struck at the same distance:\n");

    for (const bool reversed : {false, true}) {
        Scene scene;
        const EntityId lower  = scene.createEntity();
        const EntityId higher = scene.createEntity();

        // The same two boxes, stored in either order; the slots stay as made.
        for (const EntityId id : {reversed ? higher : lower, reversed ? lower : higher}) {
            scene.add<Transform>(id, Transform{});
            Rigidbody rb;
            rb.motion = RigidbodyMotion::Static;
            scene.add<Rigidbody>(id, std::move(rb));
            ColliderPart part;
            part.shape       = ColliderShape::Box;
            part.halfExtents = {0.5f, 0.5f, 0.5f};
            Collider collider;
            collider.parts = {part};
            scene.add<Collider>(id, std::move(collider));
        }

        RayHit ray;
        RayHit sweep;
        const bool rayHit   = raycast(scene, {0.0f, 5.0f, 0.0f}, {0, -1, 0}, 10.0f, ray);
        const bool sweepHit = spherecast(scene, {0.0f, 5.0f, 0.0f}, 0.2f, {0, -1, 0}, 10.0f, sweep);
        check(
            reversed ? "  and stored the other way round" : "a ray and a sweep name the lower slot",
            rayHit && ray.entity == lower && sweepHit && sweep.entity == lower
        );
    }
}

// A sweep asks not whether something is in the way, but where a body of a given
// size would come to rest.
void testSpherecast() {
    std::printf("Spherecast:\n");

    Scene scene;
    const EntityId box = addBody(scene, {5.0f, 0.0f, 0.0f}, ColliderShape::Box);
    RayHit hit;

    check(
        "a swept sphere stops short of the face by its radius",
        spherecast(scene, {0,0,0}, 0.25f, {1,0,0}, 100.0f, hit) && nearly(hit.distance, 4.25f)
    );
    check("  and reports the surface, not its own centre", nearly(hit.point.x, 4.5f));
    check("  the box entity is reported", hit.entity == box);

    // The ray passes over the box; the sphere is wide enough to catch its edge.
    check(
        "a ray at 0.6 clears a box of half extent 0.5",
        !raycast(scene, {0.0f, 0.6f, 0.0f}, {1,0,0}, 100.0f, hit)
    );
    check(
        "a sphere of 0.25 at the same height does not",
        spherecast(scene, {0.0f, 0.6f, 0.0f}, 0.25f, {1,0,0}, 100.0f, hit)
    );
    check("  stopping on the edge it clips", nearly(hit.distance, 4.27087f));

    // Aimed at a corner, where a slab test alone would stop the sphere on a face
    // plane it never reaches.
    Scene corner;
    addBody(corner, {0.0f, 0.0f, 0.0f}, ColliderShape::Box);
    const float diagonal = std::sqrt(3.0f * 4.5f * 4.5f);
    check(
        "a sweep into a corner stops on the corner",
        spherecast(corner, {5,5,5}, 0.25f, {-1,-1,-1}, 100.0f, hit) && nearly(hit.distance, diagonal - 0.25f)
    );

    Scene capsule;
    addBody(capsule, {-5.0f, 0.0f, 0.0f}, ColliderShape::Capsule);
    check(
        "a swept sphere stops a combined radius from a capsule axis",
        spherecast(capsule, {0,0,0}, 0.25f, {-1,0,0}, 100.0f, hit) && nearly(hit.distance, 4.25f)
    );

    check(
        "a sweep starting overlapped stops where it started",
        spherecast(scene, {5.0f, 0.0f, 0.0f}, 0.25f, {1,0,0}, 100.0f, hit) && nearly(hit.distance, 0.0f)
    );
    check("a sweep away from everything misses", !spherecast(scene, {0,0,0}, 0.25f, {-1,0,0}, 100.0f, hit));
    check("a sweep stopping short misses", !spherecast(scene, {0,0,0}, 0.25f, {1,0,0}, 4.0f, hit));

    // A radius of zero is a ray; answering it twice invites disagreement at the edges.
    RayHit asRay;
    raycast(scene, {0,0,0}, {1,0,0}, 100.0f, asRay);
    check(
        "a zero radius defers to raycast",
        spherecast(scene, {0,0,0}, 0.0f, {1,0,0}, 100.0f, hit) && nearly(hit.distance, asRay.distance)
    );
}

// A mesh part met as a capsule of the part's radius would make a sweep at a mesh
// floor miss a floor that is there.
void testQueryAgainstMeshes() {
    std::printf("Queries against meshes:\n");

    RayHit hit;

    // The case step-up depends on: probing a triangle floor.
    Scene terrain;
    addMeshBody(terrain, makeGridMesh(16, 16.0f));

    check(
        "a ray finds a triangle mesh below it",
        raycast(terrain, {3.0f, 4.0f, -2.0f}, {0,-1,0}, 100.0f, hit)
    );
    check("  at the surface", nearly(hit.distance, 4.0f));
    check("  facing up out of it", hit.normal.y > 0.9f);

    check(
        "a sweep finds it too, a radius short",
        spherecast(terrain, {3.0f, 4.0f, -2.0f}, 0.5f, {0,-1,0}, 100.0f, hit) && nearly(hit.distance, 3.5f)
    );

    check(
        "a ray past the edge of the mesh finds nothing",
        !raycast(terrain, {20.0f, 4.0f, 0.0f}, {0,-1,0}, 100.0f, hit)
    );
}

// The ground nine metres ahead from eye height. At a grazing angle, a march stepping
// by the gap closes only a sine's worth per step and stops millimetres above the
// surface; rays and sweeps against triangles and boxes are solved exactly instead.
void testAGrazingCastFindsTheGround() {
    std::printf("Casts at a grazing angle:\n");

    constexpr float EYE = 1.6f;
    constexpr float RADIUS = 0.25f;
    RayHit hit;

    // Two-metre cells, sixteen metres either side. Aimed inside a cell, not at a grid
    // line, so the grazing is under test and not a crack between triangles.
    Scene terrain;
    addMeshBody(terrain, makeGridMesh(16, 32.0f));

    const glm::vec3 look = {9.0f, -EYE, 0.5f};
    check(
        "a ray from eye height meets a mesh floor nine metres ahead",
        raycast(terrain, {0.0f, EYE, 0.0f}, look, 100.0f, hit)
    );
    check("  at the distance to that point", nearly(hit.distance, glm::length(look)));
    check("  and there", nearly(hit.point.x, 9.0f) && nearly(hit.point.y, 0.0f));
    check("  facing up out of it", hit.normal.y > 0.99f);

    // A sphere whose centre comes down to its radius at the same place.
    const glm::vec3 sweep = {9.0f, -(EYE - RADIUS), 0.5f};
    const float expected = glm::length(sweep);
    check(
        "a sphere swept the same way meets it too",
        spherecast(terrain, {0.0f, EYE, 0.0f}, RADIUS, sweep, 100.0f, hit) && nearly(hit.distance, expected)
    );
    check("  touching the surface under its centre", nearly(hit.point.y, 0.0f) && hit.normal.y > 0.99f);

    // A box floor, at the same grazing angle.
    Scene flat;
    addBox(flat, {0.0f, -0.5f, 0.0f}, {20.0f, 0.5f, 20.0f});
    check(
        "a sphere swept at a box floor meets it",
        spherecast(flat, {0.0f, EYE, 0.0f}, RADIUS, sweep, 100.0f, hit) && nearly(hit.distance, expected)
    );
    check("  on its top face", hit.normal.y > 0.99f);
}

} // namespace

void runPhysicsQueryTests() {
    testRaycastShapes();
    testRaycastMisses();
    testRaycastFilters();
    testAQueryTieGoesToTheLowerSlot();
    testSpherecast();
    testQueryAgainstMeshes();
    testAGrazingCastFindsTheGround();
}
