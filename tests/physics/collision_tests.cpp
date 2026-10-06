#include "physics/physics_support.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "ecs/component/physics/ragdoll.h"
#include "system/physics/authoring/collider_fit.h"
#include "resource/generate/mesh_generators.h"
#include "system/physics/authoring/ragdoll_build.h"
#include "system/physics/character/character_controller_system.h"
#include "system/physics/physics_events.h"
#include "system/physics/physics_system.h"
#include "system/physics/query/query.h"

namespace {

// Segment-to-box closest point cannot alternate projections between the two: that has
// fixed points that are not the nearest pair, reporting a real overlap as no contact -
// a capsule passing through what it touched. These overlaps (2, 4 and 11 mm) are cases
// that method misses.
void testCapsuleBoxFindsSmallOverlaps() {
    std::printf("Capsule against box, barely touching:\n");

    struct Case {
        glm::vec3 half;
        glm::vec3 a;
        glm::vec3 b;
        float radius;
    };
    const Case missedBefore[] = {
        {
            {1.070485f, 1.059132f, 1.263437f},
            {-1.577023f, -2.536281f, 2.629774f},
            {-1.436692f, 0.909800f, -0.901492f},
            0.368448f
        },
        {
            {1.454872f, 0.293407f, 1.480604f},
            {-1.342335f, 2.620376f, 2.097023f},
            {-1.542812f, -0.073972f, 0.878635f},
            0.064073f
        },
        {
            {0.692265f, 1.117279f, 0.760827f},
            {0.963844f, 0.254541f, -2.078951f},
            {0.781100f, 1.121073f, 2.809511f},
            0.176733f
        },
    };

    int missed = 0;
    for (const Case& c : missedBefore) {
        BoxShape box;
        box.center = {0.0f, 0.0f, 0.0f};
        box.halfExtents = c.half;
        CapsuleShape capsule;
        capsule.a = c.a;
        capsule.b = c.b;
        capsule.radius = c.radius;
        Contact out[MAX_CONTACTS_PER_MANIFOLD];
        if (contactCapsuleBox(capsule, box, out) == 0) ++missed;
    }
    check("a capsule overlapping a box by millimetres is a contact", missed == 0);

    // It still says no when nothing is there.
    BoxShape unit;
    unit.center = {0.0f, 0.0f, 0.0f};
    unit.halfExtents = {0.5f, 0.5f, 0.5f};
    CapsuleShape clear;
    clear.a = {3.0f, 0.0f, 0.0f};
    clear.b = {3.0f, 1.0f, 0.0f};
    clear.radius = 0.2f;
    Contact none[MAX_CONTACTS_PER_MANIFOLD];
    check("  and one well clear of it is not", contactCapsuleBox(clear, unit, none) == 0);
}

// A capsule whose segment runs through a box has no separating direction, so a face is
// picked. Picked by the one closest point found - where a through-segment entered - a
// capsule sunk in a slab with both ends out its sides would be pushed sideways by the
// whole slab instead of up by its sink depth.
void testASunkCapsuleLeavesByItsShallowestFace() {
    std::printf("A capsule lying sunk through a slab:\n");

    BoxShape slab;
    slab.halfExtents = {0.5f, 0.3f, 2.0f};

    // Along X, 25 cm below the slab's top, both ends past its sides.
    CapsuleShape sunk;
    sunk.a = {-1.0f, 0.05f, 0.0f};
    sunk.b = { 1.0f, 0.05f, 0.0f};
    sunk.radius = 0.2f;

    Contact out[MAX_CONTACTS_PER_MANIFOLD];
    const int count = contactCapsuleBox(sunk, slab, out);
    bool upward = count > 0;
    for (int i = 0; i < count; ++i) {
        upward = upward && sameDirection(out[i].normal, {0.0f, -1.0f, 0.0f});
    }
    check("it is pushed out through the top", upward);
    check("  by what it has sunk plus its radius", count > 0 && nearly(out[0].penetration, 0.45f));
    check("  held along its length, lying flat", count == 2);
}

// GJK answers only whether two convex shapes overlap; a mesh triangle asks it before
// building its manifold, so a wrong "apart" drops a body through the floor.
void testGjk() {
    std::printf("GJK:\n");

    const BoxShape unit = boxAt({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f});

    const glm::vec3 half = {0.5f, 0.5f, 0.5f};
    check(
        "boxes clear of each other do not overlap",
        !gjkOverlap(supportOf(unit), supportOf(boxAt({2.0f, 0, 0}, half)))
    );
    check("boxes sharing space do", gjkOverlap(supportOf(unit), supportOf(boxAt({0.5f, 0, 0}, half))));

    std::printf("GJK - shapes SAT cannot pair:\n");

    CapsuleShape capsule;
    capsule.a = {0.0f, -0.4f, 0.0f};
    capsule.b = {0.0f,  0.4f, 0.0f};
    capsule.radius = 0.3f;
    check("a capsule through a box overlaps", gjkOverlap(supportOf(unit), supportOf(capsule)));

    CapsuleShape sphere;
    sphere.a = {1.6f, 0.0f, 0.0f};
    sphere.b = sphere.a;
    sphere.radius = 0.3f;
    check("a sphere clear of it does not", !gjkOverlap(supportOf(unit), supportOf(sphere)));
    sphere.a = {0.7f, 0.0f, 0.0f};
    sphere.b = sphere.a;
    check("a sphere touching it does", gjkOverlap(supportOf(unit), supportOf(sphere)));

    // A point cloud is its own hull to a support query, so a mesh triangle needs no
    // routine of its own.
    const glm::vec3 tetra[4] = {
        {0.4f, 0.0f, 0.0f}, {1.4f, 0.0f, 0.0f},
        {0.9f, 1.0f, 0.0f}, {0.9f, 0.5f, 1.0f}
    };
    check("a point cloud overlapping the box does", gjkOverlap(supportOf(unit), supportOfPoints(tetra, 4)));

    const glm::vec3 far_[3] = {{3.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {3.5f, 1.0f, 0.0f}};
    check("a triangle well clear of it does not", !gjkOverlap(supportOf(unit), supportOfPoints(far_, 3)));

    // Concentric: the origin is inside the difference from the first step, where a
    // search assuming it starts outside falls over.
    check("boxes exactly on top of each other overlap", gjkOverlap(supportOf(unit), supportOf(unit)));
}

// Two things can share a world without touching: what a layer says.
void testCollisionLayers() {
    std::printf("Collision layers:\n");

    constexpr int LEVEL = 1 << 0;
    constexpr int BONES = 1 << 1;

    // A body dropped on ground it does not collide with falls straight past.
    Scene ignored;
    const EntityId floor =
        addBox(ignored, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    ignored.get<Rigidbody>(floor).layer = LEVEL;

    const EntityId ghost = addFallingBody(ignored, {0.0f, 3.0f, 0.0f}, 0.5f);
    ignored.get<Rigidbody>(ghost).layer = BONES;
    ignored.get<Rigidbody>(ghost).collidesWith = ~LEVEL;

    simulate(ignored, 180);
    check("a body falls through what its mask excludes", ignored.get<Transform>(ghost).position.y < -2.0f);

    // The mask is read both ways: excluding from either side suffices, or "does A hit
    // B" would depend on which was asked.
    Scene oneSided;
    const EntityId floor2 =
        addBox(oneSided, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    oneSided.get<Rigidbody>(floor2).layer = LEVEL;
    oneSided.get<Rigidbody>(floor2).collidesWith = ~BONES;

    const EntityId falling = addFallingBody(oneSided, {0.0f, 3.0f, 0.0f}, 0.5f);
    oneSided.get<Rigidbody>(falling).layer = BONES;

    simulate(oneSided, 180);
    check("  and excluding from one side is enough", oneSided.get<Transform>(falling).position.y < -2.0f);

    // Unset, everything collides, so code that never heard of layers is unchanged.
    Scene plain;
    addBox(plain, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId lands = addFallingBody(plain, {0.0f, 3.0f, 0.0f}, 0.5f);
    simulate(plain, 180);
    check("  while the defaults collide with everything", plain.get<Transform>(lands).position.y > 0.0f);

    // A query takes the same mask, so "the level, not the characters" is asked for,
    // not listed entity by entity.
    Scene mixed;
    const EntityId ground =
        addBox(mixed, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    mixed.get<Rigidbody>(ground).layer = LEVEL;
    const EntityId limb = addBox(mixed, {0.0f, 1.0f, 0.0f}, {0.4f, 0.4f, 0.4f});
    mixed.get<Rigidbody>(limb).layer = BONES;

    RayHit hit;
    check(
        "a ray with no mask hits whatever is nearest",
        raycast(mixed, {0.0f, 5.0f, 0.0f}, {0,-1,0}, 20.0f, hit) && hit.entity == limb
    );

    QueryFilter levelOnly;
    levelOnly.layerMask = LEVEL;
    check(
        "  and a masked one looks past what it excludes",
        raycast(mixed, {0.0f, 5.0f, 0.0f}, {0,-1,0}, 20.0f, hit, levelOnly) && hit.entity == ground
    );

    // The ragdoll build: bones on their own layer, cleared from the owner's mask, so a
    // rig is never pushed by its own limbs.
    Scene rigged;
    const EntityId owner = addFallingBody(rigged, {0.0f, 1.0f, 0.0f}, 0.3f);
    const uint32_t built = buildRagdoll(rigged, owner, makeTestRig());
    check("a ragdoll puts its bones on their own layer", built == 4);
    const int boneLayer = RagdollSettings{}.boneLayer;
    const int ownerMask = rigged.get<Rigidbody>(owner).collidesWith;
    check("  and takes that layer out of the owner's mask", (ownerMask & boneLayer) == 0);
    check("  leaving the owner colliding with everything else", (ownerMask & ~boneLayer) == ~boneLayer);

    // Bones do not hit each other: each capsule spans to its child, so limbs overlap
    // as built, and a self-colliding rig throws itself apart on the first tick.
    bool anyBoneSelfCollides = false;
    for (const RagdollBone& bone : rigged.get<Ragdoll>(owner).bones) {
        const Rigidbody& body = rigged.get<Rigidbody>(bone.body);
        if ((body.layer & body.collidesWith) != 0) anyBoneSelfCollides = true;
    }
    check("  and the bones do not collide with each other", !anyBoneSelfCollides);

    // And gives it back: collidesWith is authored and serialized, so a character that
    // once had a ragdoll must not quietly stop colliding with a layer.
    clearRagdoll(rigged, owner);
    check(
        "  and clearing the ragdoll returns the layer to the owner",
        (rigged.get<Rigidbody>(owner).collidesWith & boneLayer) == boneLayer
    );
}

// A body resting on a narrower support. Two horizontal edges cross to a vertical axis
// duplicating the face normal, and under a whisker of tilt float noise can hand the SAT
// that duplicate: the four-point manifold becomes one corner, and the body rocks on it.
void testRestOnNarrowSupport() {
    std::printf("Rest on a narrow support:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    addBox(scene, {0.0f, 0.3f, 0.0f}, {0.25f, 0.3f, 1.2f});   // the fulcrum

    const EntityId plank = scene.createEntity();
    Transform t;
    t.position = {0.0f, 0.82f, 0.0f};
    scene.add<Transform>(plank, std::move(t));
    Rigidbody rb;
    rb.mass = 35.0f;
    scene.add<Rigidbody>(plank, std::move(rb));
    ColliderPart part;
    part.shape = ColliderShape::Box;
    part.halfExtents = {3.5f, 0.12f, 1.0f};
    Collider c;
    c.parts = { part };
    scene.add<Collider>(plank, std::move(c));

    simulate(scene, 600);

    const Rigidbody& out = scene.get<Rigidbody>(plank);
    const float restY = scene.get<Transform>(plank).position.y;
    check(
        "a plank on a narrow fulcrum comes to rest",
        glm::length(out.linearVelocity) < 0.01f && glm::length(out.angularVelocity) < 0.05f
    );
    check("  on top of it, not sunk into it", restY > 0.70f);
    check("  and sleeps there", out.sleeping);
}

void testMeshCollider() {
    std::printf("Mesh collider:\n");

    const MeshAsset grid = makeGridMesh(16, 16.0f);

    Collider probe;
    probe.parts.clear();
    const uint32_t triangles = addMeshCollider(probe, addTestMesh(grid), meshLibrary());
    check("a mesh becomes triangles", triangles == 16 * 16 * 2);
    check("  stored three points each", probe.meshPoints.size() == triangles * 3);
    check("  with a hierarchy over them", !probe.meshNodes.empty());

    // The tree must cull: a metre-wide query over a sixteen-metre floor reaches a handful
    // of triangles.
    std::vector<uint32_t> found;
    queryMeshBvh(probe.meshNodes, {-0.5f, -1.0f, -0.5f}, {0.5f, 1.0f, 0.5f}, found);
    check("  that culls rather than returning everything", !found.empty() && found.size() < triangles / 4);

    // A far-away query reaches nothing at all.
    found.clear();
    queryMeshBvh(probe.meshNodes, {100.0f, 0.0f, 100.0f}, {101.0f, 1.0f, 101.0f}, found);
    check("  and finds nothing where there is nothing", found.empty());

    // A body falling onto a triangle floor must stop on it: broadphase, tree walk, then
    // GJK per triangle returned.
    Scene scene;
    addMeshBody(scene, grid);
    const EntityId box = addFallingBody(scene, {0.0f, 3.0f, 0.0f}, 0.5f);

    simulate(scene, 240);
    const glm::vec3 rest = scene.get<Transform>(box).position;
    check("a body lands on a triangle mesh", rest.y > 0.25f && rest.y < 1.0f);
    check("  without sliding off it", glm::length(glm::vec2(rest.x, rest.z)) < 1.0f);

    // Away from the origin, where bodies actually land on a sixteen-metre floor; passing
    // only above the origin would be passing by test placement.
    Scene offCentre;
    addMeshBody(offCentre, grid);
    const EntityId away = addFallingBody(offCentre, {5.0f, 3.0f, -4.0f}, 0.5f);
    simulate(offCentre, 240);
    check("  and lands away from the origin too", offCentre.get<Transform>(away).position.y > 0.25f);

    // Off the mesh's edge a body falls past, not caught by a triangle it never met.
    Scene edge;
    addMeshBody(edge, grid);
    const EntityId beyond = addFallingBody(edge, {20.0f, 3.0f, 0.0f}, 0.5f);
    simulate(edge, 180);
    check("  and falls past where the mesh is not", edge.get<Transform>(beyond).position.y < -1.0f);
}

// A triangle is a zero-thickness hull, so its Minkowski difference with a body is
// symmetric about its plane: the shallowest way out flips once the body's centre
// crosses it, driving the body down through the floor. Started sunk, as a fast body
// or a bad tick arrives.
void testMeshDoesNotEjectDownward() {
    std::printf("A body sunk into a mesh floor:\n");

    Scene scene;
    addMeshBody(scene, makeGridMesh(8, 12.0f));

    // Centre below the plane, where a triangle's normal flips.
    const EntityId sunk = addFallingBody(scene, {0.5f, -0.12f, 0.5f}, 0.5f);
    simulate(scene, 300);

    const float y = scene.get<Transform>(sunk).position.y;
    check("it is pushed back up, not through", y > 0.0f);
    check("  and comes to rest on the surface", y > 0.25f && y < 1.0f);
}

// Met one support point at a time along the face normal, whose ties break one way, a
// box flat on a mesh floor would get the same corner from every triangle and balance
// on it; a lying capsule always the same end. The manifold comes from the feature
// facing the triangle.
void testAShapeRestsOnATriangleByItsWholeFace() {
    std::printf("What a triangle holds a shape by:\n");

    // One upward triangle, far wider than anything put on it.
    const glm::vec3 floor[3] = {{-10.0f, 0.0f, -10.0f}, {-10.0f, 0.0f, 30.0f}, {30.0f, 0.0f, -10.0f}};
    Contact out[MAX_CONTACTS_PER_MANIFOLD];

    // A box a centimetre into it.
    const BoxShape box = boxAt({0.0f, 0.49f, 0.0f}, {0.5f, 0.5f, 0.5f});
    const int held = contactTriangle(floor, supportOf(box), out);
    check("a box flat on a triangle is held at four points", held == 4);

    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    bool upward = true;
    for (int i = 0; i < held; ++i) {
        lo = glm::min(lo, out[i].point);
        hi = glm::max(hi, out[i].point);
        upward = upward && sameDirection(out[i].normal, {0.0f, 1.0f, 0.0f});
    }
    check(
        "  one under each corner of its face",
        held == 4 && lo.x < -0.49f && hi.x > 0.49f && lo.z < -0.49f && hi.z > 0.49f
    );
    check("  each facing up out of the triangle", upward);
    check("  each as deep as the box is into it", held == 4 && nearly(out[0].penetration, 0.01f));

    // A capsule lying along X, a centimetre in.
    CapsuleShape lying;
    lying.a = {-0.5f, 0.29f, 0.0f};
    lying.b = { 0.5f, 0.29f, 0.0f};
    lying.radius = 0.3f;
    const int ends = contactTriangle(floor, supportOf(lying), out);
    check("a capsule lying flat is held at both ends", ends == 2);
    check(
        "  one at each",
        ends == 2
            && std::fabs(out[0].point.x + out[1].point.x) < 1e-3f
            && std::fabs(out[0].point.x - out[1].point.x) > 0.99f
    );

    // Standing on end, it touches at one point.
    CapsuleShape standing;
    standing.a = {0.0f, 0.29f, 0.0f};
    standing.b = {0.0f, 1.29f, 0.0f};
    standing.radius = 0.3f;
    check("a capsule on end is held at one", contactTriangle(floor, supportOf(standing), out) == 1);

    // Judged by |e1 x e2|^2 (metres^4) against a squared metre-scale tolerance, every
    // triangle with legs under three centimetres would be a sliver, and a finely
    // tessellated floor would hold nothing up.
    const glm::vec3 fine[3] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.02f}, {0.02f, 0.0f, 0.0f}};
    CapsuleShape pebble;
    pebble.a = {0.005f, 0.099f, 0.005f};
    pebble.b = pebble.a;
    pebble.radius = 0.1f;
    const int touched = contactTriangle(fine, supportOf(pebble), out);
    check("a two-centimetre triangle still has a face", touched == 1);
    check("  facing up out of it", touched == 1 && sameDirection(out[0].normal, {0.0f, 1.0f, 0.0f}));

    // A true sliver is still refused: three corners on one line have no face.
    const glm::vec3 sliver[3] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}};
    check("  while three corners on a line have none", contactTriangle(sliver, supportOf(pebble), out) == 0);

    // Over a whole floor: a dropped box settles square and sleeps, not rocking on
    // whichever corner won.
    Scene scene;
    addMeshBody(scene, makeGridMesh(8, 8.0f));
    const EntityId dropped = addFallingBody(scene, {0.3f, 1.0f, 0.2f}, 0.5f);
    scene.get<Rigidbody>(dropped).canSleep = true;
    simulate(scene, 300);

    const Transform& rest = scene.get<Transform>(dropped);
    const glm::vec3 up = rest.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
    check(
        "a box dropped on a mesh floor rests level",
        up.y > std::cos(glm::radians(1.0f)) && rest.position.y > 0.45f
    );
    check("  and falls asleep there", scene.get<Rigidbody>(dropped).sleeping);
}

// A shape touching a ledge from the side meets its top triangle only across the edge,
// where the depth along that normal is the step's whole height. Taken as a contact, a
// capsule brushing a half-metre riser would be launched up onto it.
void testALedgeRiserDoesNotLaunchWhatBrushesIt() {
    std::printf("A mesh ledge met from the side:\n");

    // The top of a half-metre ledge over x in [0, 2], facing up.
    const glm::vec3 top[3] = {{0.0f, 0.5f, -1.0f}, {0.0f, 0.5f, 1.0f}, {2.0f, 0.5f, 1.0f}};
    Contact out[MAX_CONTACTS_PER_MANIFOLD];

    // Each a centimetre into the riser's plane, x = 0.
    CapsuleShape brushing;
    brushing.a = {-0.29f, 0.3f, 0.2f};
    brushing.b = {-0.29f, 1.5f, 0.2f};
    brushing.radius = 0.3f;
    check(
        "a capsule against the riser gets nothing from the top",
        contactTriangle(top, supportOf(brushing), out) == 0
    );

    const BoxShape pressed = boxAt({-0.49f, 0.5f, 0.2f}, {0.5f, 0.5f, 0.5f});
    check("  nor does a box", contactTriangle(top, supportOf(pressed), out) == 0);

    // Past the edge, standing on the top, it is still the top that holds it.
    CapsuleShape standing = brushing;
    standing.a = {0.1f, 0.79f, 0.2f};
    standing.b = {0.1f, 1.79f, 0.2f};
    const int held = contactTriangle(top, supportOf(standing), out);
    check(
        "  while one standing on it is held up",
        held == 1 && sameDirection(out[0].normal, {0.0f, 1.0f, 0.0f}) && nearly(out[0].penetration, 0.01f)
    );

    // Walked into, as a level is: top and riser as one mesh.
    MeshAsset ledge;
    const glm::vec3 corners[] = {
        {1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 0.5f, 1.0f}, {1.0f, 0.5f, -1.0f},
        {3.0f, 0.5f, 1.0f},  {3.0f, 0.5f, -1.0f}
    };
    for (const glm::vec3& corner : corners) {
        Vertex vertex;
        vertex.position = corner;
        ledge.vertices.push_back(vertex);
    }
    ledge.indices = {
        0, 1, 2,  0, 2, 3,    // riser, facing -X
        3, 2, 4,  3, 4, 5     // top, facing up
    };

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {10.0f, 0.5f, 10.0f});
    addMeshBody(scene, ledge);
    const EntityId walker = addCharacter(scene, 0.3f, 0.6f, 0.0f);
    const float standingY = scene.get<Transform>(walker).position.y;

    TestFrame frame(scene, meshLibrary());
    PhysicsSystem physics;
    CharacterControllerSystem controller;
    float highest = standingY;
    for (int tick = 0; tick < 120; ++tick) {
        physics.fixedUpdate(frame.ctx);
        controller.fixedUpdate(frame.ctx);
        highest = glm::max(highest, scene.get<Transform>(walker).position.y);
    }
    check("a character walking into the ledge stays on the floor", highest < standingY + 0.05f);
    check("  held at the riser, not through it", scene.get<Transform>(walker).position.x < 1.0f);
}

// A mesh part with its own centre. The tree is built over the raw points, so the query
// bound and the triangle placement must come back to that space through the same two
// terms - body pose and part centre. Applying one but not the other offsets them by the
// centre; the slab test misses, and the body falls through a floor that is there.
void testOffsetMeshPartCollides() {
    std::printf("A mesh part offset from its entity:\n");

    Scene scene;
    const MeshAsset grid = makeGridMesh(8, 12.0f);

    const EntityId floorId = scene.createEntity();
    scene.add<Transform>(floorId, Transform{});
    Rigidbody floorBody;
    floorBody.motion = RigidbodyMotion::Static;
    scene.add<Rigidbody>(floorId, std::move(floorBody));

    Collider collider;
    collider.parts.clear();
    check("a mesh to stand on", addMeshCollider(collider, addTestMesh(grid), meshLibrary()) > 0);
    // Shifted two metres up: triangles at y = 0 in their own points now sit at y = 2.
    collider.parts[0].center = {0.0f, 2.0f, 0.0f};
    scene.add<Collider>(floorId, std::move(collider));

    const EntityId faller = addFallingBody(scene, {0.5f, 6.0f, 0.5f}, 0.4f);
    simulate(scene, 400);

    const float y = scene.get<Transform>(faller).position.y;
    check("  a body lands on it where the offset puts it", y > 2.0f && y < 3.0f);
    check("  rather than falling through it", y > 0.0f);
}

// The median split built only from steps the standard defines: widest axis, then a
// stable centroid sort along it, keeping ties in arrival order. Slow, so the engine's
// tree has a judge other than itself.
void referenceSplit(std::vector<glm::vec3>& points, uint32_t first, uint32_t count) {
    if (count <= 4) return;

    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());
    for (uint32_t i = first * 3; i < (first + count) * 3; ++i) {
        low  = glm::min(low, points[i]);
        high = glm::max(high, points[i]);
    }
    const glm::vec3 extent = high - low;
    int axis = 0;
    if (extent.y > extent[axis]) axis = 1;
    if (extent.z > extent[axis]) axis = 2;

    const auto centroid = [&](uint32_t t) {
        return ((points[t * 3] + points[t * 3 + 1] + points[t * 3 + 2]) / 3.0f)[axis];
    };
    std::vector<uint32_t> order;
    for (uint32_t t = first; t < first + count; ++t) order.push_back(t);
    std::stable_sort(
        order.begin(),
        order.end(),
        [&](uint32_t a, uint32_t b) { return centroid(a) < centroid(b); }
    );

    std::vector<glm::vec3> sorted;
    for (uint32_t t : order) {
        for (uint32_t c = 0; c < 3; ++c) sorted.push_back(points[t * 3 + c]);
    }
    std::copy(sorted.begin(), sorted.end(), points.begin() + first * 3);

    referenceSplit(points, first, count / 2);
    referenceSplit(points, first + count / 2, count - count / 2);
}

// A tree's triangle order is the order of a body's contacts with them, and the solver's
// answer depends on it. So every standard library must build the same tree, which a
// split leaving ties wherever nth_element puts them does not: a grid is all ties.
void testAMeshBuildsOneTreeEverywhere() {
    std::printf("The tree over a mesh, against one every library builds alike:\n");

    const MeshAsset grid = makeGridMesh(12, 12.0f);
    std::vector<glm::vec3> points;
    for (uint32_t index : grid.indices) points.push_back(grid.vertices[index].position);

    std::vector<glm::vec3> built = points;
    const std::vector<MeshNode> nodes = buildMeshBvh(built);
    std::vector<glm::vec3> expected = points;
    referenceSplit(expected, 0, static_cast<uint32_t>(points.size() / 3));

    check("a tree is built", !nodes.empty());
    check("  with the triangles in the order a stable median split puts them", built == expected);
}

// A mesh collider names its mesh and collides with its triangles as they are now:
// re-imported or rescaled, it follows, rather than keeping what was saved.
void testAMeshColliderFollowsItsMesh() {
    std::printf("A mesh collider and the mesh it names:\n");

    Collider box;
    ColliderPart part;
    part.shape = ColliderShape::Box;
    part.halfExtents = {0.5f, 0.5f, 0.5f};
    box.parts = { part };
    check(
        "a box collider has nothing to build",
        !syncMeshCollider(box, meshLibrary()) && box.meshNodes.empty() && box.parts.size() == 1
    );

    Collider empty;
    empty.parts.clear();
    check(
        "  nor has one with no parts at all",
        !syncMeshCollider(empty, meshLibrary()) && empty.meshNodes.empty()
    );

    const MeshHandle handle = addTestMesh(makeGridMesh(4, 4.0f));
    Collider floor;
    floor.parts.clear();
    check("a mesh part is built from its mesh", addMeshCollider(floor, handle, meshLibrary()) == 4 * 4 * 2);
    check("  and asking again with nothing changed builds nothing", !syncMeshCollider(floor, meshLibrary()));

    // Re-imported larger: the same handle, new contents, a new version.
    MeshAsset larger = makeGridMesh(8, 4.0f);
    meshLibrary().swapValue(handle, larger);
    check(
        "a mesh that changes is built again",
        syncMeshCollider(floor, meshLibrary()) && floor.meshPoints.size() == 8 * 8 * 2 * 3
    );

    floor.parts[0].meshScale = {2.0f, 1.0f, 2.0f};
    syncMeshCollider(floor, meshLibrary());
    check("  as is one whose scale changes", !floor.meshNodes.empty() && floor.meshNodes[0].max.x > 3.9f);

    // A handle the graph cannot answer - mesh removed, or loaded missing - is no
    // triangles, not the last ones built.
    floor.parts[0].mesh = MeshHandle{};
    check(
        "one whose mesh is gone collides with nothing",
        syncMeshCollider(floor, meshLibrary()) && floor.meshPoints.empty() && floor.meshNodes.empty()
    );
}

// Which body of a pair is A decides the normal's direction, the key its impulses persist
// under between ticks, and the collision event's naming order. Taken from sweep order,
// two equal-width stacked bodies would swap on a sub-micron drift and that tick start
// from nothing. The entity slot is a fact about the pair; X position is not.
void testAPairIsOrderedByItsBodiesNotByWhereTheyAre() {
    std::printf("Which body of a pair comes first:\n");

    Scene scene;
    scene.physics().gravity = glm::vec3(0.0f);   // nothing but the overlap

    // The lower slot sits further along X, so the sweep meets it second.
    const EntityId first  = addFallingBody(scene, {0.9f, 0.0f, 0.0f}, 0.5f);
    const EntityId second = addFallingBody(scene, {0.0f, 0.0f, 0.0f}, 0.5f);
    check("the first body has the lower slot", first.slot() < second.slot());

    TestFrame frame(scene);
    FrameContext& ctx = frame.ctx;
    EventBus& events  = frame.events;

    int fired = 0;
    CollisionEvent heard;
    events.subscribe<CollisionEvent>([&](const CollisionEvent& event) {
        ++fired;
        heard = event;
    });

    PhysicsSystem physics;
    physics.fixedUpdate(ctx);
    events.flush();

    check("  the overlap is one collision", fired == 1);
    check("  naming the lower slot first, wherever it stands", heard.a == first);
    check("  with the normal running from it to the other", heard.normal.x < -0.9f);
}

// A trigger that cannot move must still see what moves through it. The broadphase skips
// pairs whose ends are both immovable, and `immovable` covers kinematic as well as
// static, so that rule alone would skip a static checkpoint and a kinematic platform
// patrolling into it. A dynamic body is seen either way.
void testStaticTriggerSeesKinematic() {
    std::printf("A trigger that cannot move itself:\n");

    Scene scene;

    const EntityId gate = addBox(scene, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    scene.get<Collider>(gate).isTrigger = true;

    // Kinematic, not static: a patrolling platform is the case this is about.
    const EntityId platform = addBox(scene, {0.5f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f});
    Rigidbody& moving = scene.get<Rigidbody>(platform);
    moving.motion = RigidbodyMotion::Kinematic;

    TestFrame frame(scene);
    FrameContext& ctx = frame.ctx;
    EventBus& events  = frame.events;

    int fired = 0;
    EntityId reportedTrigger;
    EntityId reportedOther;
    events.subscribe<TriggerEvent>([&](const TriggerEvent& event) {
        ++fired;
        reportedTrigger = event.trigger;
        reportedOther   = event.other;
    });

    PhysicsSystem physics;
    physics.fixedUpdate(ctx);
    events.flush();

    check("it sees the kinematic body standing in it", fired == 1);
    check("and names itself as the trigger",           reportedTrigger == gate);
    check("and the body that entered as the other",    reportedOther == platform);

    // A trigger resolves nothing: had the pair reached the solver, the platform would
    // have been pushed out.
    check("and does not push it out", nearly(scene.get<Transform>(platform).position.x, 0.5f));
}

// A trigger resting on the static floor would report {trigger, floor} every tick for
// the level's life - an event flood for every listener and a collision per tick. A
// sensor senses what moves, as Box2D's and Jolt's do.
void testATriggerIgnoresTheLevelItSitsIn() {
    std::printf("A trigger sitting on the floor:\n");

    Scene scene;
    const EntityId floor = addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId gate  = addBox(scene, {0.0f, 0.9f, 0.0f}, {1.0f, 1.0f, 1.0f});
    scene.get<Collider>(gate).isTrigger = true;

    const EntityId walker = addBox(scene, {0.0f, 0.5f, 0.0f}, {0.3f, 0.5f, 0.3f});
    scene.get<Rigidbody>(walker).motion = RigidbodyMotion::Kinematic;

    TestFrame frame(scene);
    int heardWalker = 0;
    int heardFloor  = 0;
    frame.events.subscribe<TriggerEvent>([&](const TriggerEvent& event) {
        if (event.other == walker) ++heardWalker;
        if (event.other == floor)  ++heardFloor;
    });

    PhysicsSystem physics;
    for (int tick = 0; tick < 10; ++tick) {
        physics.fixedUpdate(frame.ctx);
        frame.events.flush();
    }
    check("it never reports the floor it overlaps", heardFloor == 0);
    check("  and reports what stands in it once a tick", heardWalker == 10);
}

// One tick's contact report: counts per phase, and every collision event in order heard.
struct ContactLog {
    int began  = 0;
    int stayed = 0;
    int ended  = 0;
    std::vector<CollisionEvent> collisions;

    void count(ContactPhase phase) {
        if (phase == ContactPhase::Began)  ++began;
        if (phase == ContactPhase::Stayed) ++stayed;
        if (phase == ContactPhase::Ended)  ++ended;
    }

    int total() const { return began + stayed + ended; }

    void clear() { *this = ContactLog{}; }
};

void listenToCollisions(EventBus& events, ContactLog& log) {
    events.subscribe<CollisionEvent>([&log](const CollisionEvent& event) {
        log.count(event.phase);
        log.collisions.push_back(event);
    });
}

void step(PhysicsSystem& physics, TestFrame& frame) {
    physics.fixedUpdate(frame.ctx);
    frame.events.flush();
}

// A tick the server disagreed with, run again as Engine::run does.
void replay(PhysicsSystem& physics, TestFrame& frame) {
    frame.ownedNet.beginReplayTick(InputCommand{});
    physics.fixedUpdate(frame.ctx);
    frame.ownedNet.endReplay(frame.ctx.scene);
    frame.events.flush();
}

// A contact is reported as it changes: once on begin, once a tick while it lasts, once
// on end. One event per tick per pair would leave "landed" to the game to remember,
// and a lift-off unhearable.
void testAContactBeginsStaysAndEndsOnce() {
    std::printf("A box landing, resting and lifted off:\n");

    Scene scene;
    const EntityId floor = addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId box   = addFallingBody(scene, {0.0f, 1.0f, 0.0f}, 0.5f);
    scene.get<Rigidbody>(floor).restitution = 0.0f;   // landing, not bouncing
    scene.get<Rigidbody>(box).restitution   = 0.0f;

    TestFrame frame(scene);
    ContactLog tick;
    listenToCollisions(frame.events, tick);
    PhysicsSystem physics;

    int silentFall = 0;
    while (tick.total() == 0 && silentFall < 120) {
        step(physics, frame);
        if (tick.total() == 0) ++silentFall;
    }
    check("the fall reports nothing until it lands", silentFall > 5 && silentFall < 120);
    check("  and the landing is one Began", tick.began == 1 && tick.total() == 1);
    check(
        "  naming the floor and the box",
        tick.collisions.size() == 1 && tick.collisions[0].a == floor && tick.collisions[0].b == box
    );

    int stayedTicks = 0;
    bool onlyStayed = true;
    for (int i = 0; i < 60; ++i) {
        tick.clear();
        step(physics, frame);
        onlyStayed = onlyStayed && tick.stayed == 1 && tick.total() == 1;
        if (tick.stayed == 1) ++stayedTicks;
    }
    check("resting on it is one Stayed a tick, and nothing else", onlyStayed && stayedTicks == 60);
    check(
        "  carrying the contact's normal, floor to box",
        tick.collisions.size() == 1 && tick.collisions[0].normal.y > 0.9f
    );

    Rigidbody& lifted = scene.get<Rigidbody>(box);
    lifted.gravityScale   = 0.0f;
    lifted.linearVelocity = glm::vec3(0.0f);
    scene.get<Transform>(box).position.y = 3.0f;

    tick.clear();
    step(physics, frame);
    check("lifting it off is one Ended", tick.ended == 1 && tick.total() == 1);

    tick.clear();
    for (int i = 0; i < 10; ++i) step(physics, frame);
    check("  and then the world is silent", tick.total() == 0);
}

// The same three phases for a trigger, once each: a coin taking a tick's event for
// "collected" would count itself while the player stood in it.
void testATriggerReportsEnterAndExitOnce() {
    std::printf("A body walked through a trigger:\n");

    Scene scene;
    const EntityId gate = addBox(scene, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    scene.get<Collider>(gate).isTrigger = true;

    const EntityId walker = addBox(scene, {-3.05f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f});
    scene.get<Rigidbody>(walker).motion = RigidbodyMotion::Kinematic;

    TestFrame frame(scene);
    std::vector<TriggerEvent> heard;
    frame.events.subscribe<TriggerEvent>([&](const TriggerEvent& event) { heard.push_back(event); });
    PhysicsSystem physics;

    int insideTicks = 0;
    for (int i = 0; i < 21; ++i) {
        Transform& at = scene.get<Transform>(walker);
        at.position.x = -3.05f + 0.3f * static_cast<float>(i);
        if (std::fabs(at.position.x) < 1.5f) ++insideTicks;
        step(physics, frame);
    }

    int began = 0;
    int stayed = 0;
    int ended = 0;
    bool named = true;
    for (const TriggerEvent& event : heard) {
        named = named && event.trigger == gate && event.other == walker;
        if (event.phase == ContactPhase::Began)  ++began;
        if (event.phase == ContactPhase::Stayed) ++stayed;
        if (event.phase == ContactPhase::Ended)  ++ended;
    }
    check("it walked in and out", insideTicks > 2);
    check("  and the trigger heard one enter", began == 1);
    check("  one exit", ended == 1);
    check("  and a stay for every tick between", stayed == insideTicks - 1);
    check(
        "  in that order",
        !heard.empty()
            && heard.front().phase == ContactPhase::Began
            && heard.back().phase == ContactPhase::Ended
    );
    check("  each naming itself and the walker", named);
}

// A client re-runs ticks the server disagreed with, which reported their contacts the
// first time. A replay must say nothing and must not move what the next live tick is
// compared with, or a contact it briefly lost would begin again.
void testAReplayedTickReportsNothing() {
    std::printf("Contacts across a replayed tick:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId box = addFallingBody(scene, {0.0f, 0.49f, 0.0f}, 0.5f);
    scene.get<Rigidbody>(box).gravityScale = 0.0f;

    TestFrame frame(scene);
    ContactLog tick;
    listenToCollisions(frame.events, tick);
    PhysicsSystem physics;

    const auto place = [&](float y) {
        scene.get<Transform>(box).position.y = y;
        scene.get<Rigidbody>(box).linearVelocity = glm::vec3(0.0f);
    };

    step(physics, frame);
    check("a live tick reports the contact beginning", tick.began == 1 && tick.total() == 1);

    place(3.0f);
    tick.clear();
    replay(physics, frame);
    check("a replay that loses the contact reports nothing", tick.total() == 0);

    place(0.49f);
    tick.clear();
    step(physics, frame);
    check(
        "  and the next live tick finds it lasting, not begun again",
        tick.stayed == 1 && tick.total() == 1
    );

    place(3.0f);
    tick.clear();
    step(physics, frame);
    check("a live tick reports it ending", tick.ended == 1 && tick.total() == 1);

    place(0.49f);
    tick.clear();
    replay(physics, frame);
    check("a replay that finds it again reports nothing", tick.total() == 0);

    place(3.0f);
    tick.clear();
    step(physics, frame);
    check("  and the next live tick has nothing to end", tick.total() == 0);
}

// A sleeping pair is still touching. No Stayed - nothing changes until it wakes, and a
// settled pile would otherwise report every contact every tick - and no Ended: on waking
// it is the same contact.
void testASleepingPairIsQuietButNotEnded() {
    std::printf("A box asleep on the floor:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId box = addFallingBody(scene, {0.0f, 0.49f, 0.0f}, 0.5f);
    scene.get<Rigidbody>(box).canSleep = true;

    TestFrame frame(scene);
    ContactLog tick;
    listenToCollisions(frame.events, tick);
    PhysicsSystem physics;

    for (int i = 0; i < 240 && !scene.get<Rigidbody>(box).sleeping; ++i) step(physics, frame);
    check("the box falls asleep", scene.get<Rigidbody>(box).sleeping);

    tick.clear();
    for (int i = 0; i < 60; ++i) step(physics, frame);
    check("  still asleep a second later", scene.get<Rigidbody>(box).sleeping);
    check("  and its contact sends no Stayed while it sleeps", tick.stayed == 0);
    check("  and no Ended, because it is still touching", tick.ended == 0 && tick.total() == 0);

    // Woken as Rigidbody::wake wakes one.
    scene.get<Rigidbody>(box).sleeping   = false;
    scene.get<Rigidbody>(box).sleepTimer = 0.0f;
    tick.clear();
    step(physics, frame);
    check("woken, the same contact stays rather than beginning again", tick.stayed == 1 && tick.total() == 1);
}

// Destroyed while touching ends a contact like any other leaving: the survivor hears
// Ended, naming an id no longer alive.
void testADestroyedBodyEndsItsContacts() {
    std::printf("A body destroyed while touching:\n");

    Scene scene;
    const EntityId floor = addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId box   = addFallingBody(scene, {0.0f, 0.49f, 0.0f}, 0.5f);

    TestFrame frame(scene);
    ContactLog tick;
    listenToCollisions(frame.events, tick);
    PhysicsSystem physics;

    step(physics, frame);
    check("the contact began", tick.began == 1);

    scene.destroyEntity(box);
    tick.clear();
    step(physics, frame);
    check("the next tick ends it", tick.ended == 1 && tick.total() == 1);
    check(
        "  naming the floor and the dead box",
        tick.collisions.size() == 1
            && tick.collisions[0].a == floor
            && tick.collisions[0].b == box
            && !scene.isAlive(tick.collisions[0].b)
    );

    const EntityId heir = addFallingBody(scene, {0.0f, 0.49f, 0.0f}, 0.5f);
    tick.clear();
    step(physics, frame);
    check("a new body's contact begins", tick.began == 1 && tick.total() == 1);

    // Replaced between ticks by a new entity in the same slot: the old contact ends and
    // the new one begins, not one contact spanning two bodies.
    scene.destroyEntity(heir);
    const EntityId successor = addFallingBody(scene, {0.0f, 0.49f, 0.0f}, 0.5f);
    check("  the next body takes the freed slot", successor.slot() == heir.slot() && successor != heir);
    tick.clear();
    step(physics, frame);
    check(
        "  and the tick ends the dead one's contact and begins its own",
        tick.ended == 1
            && tick.began == 1
            && tick.total() == 2
            && tick.collisions[0].b == heir
            && tick.collisions[1].b == successor
    );

    // The last body going leaves nothing to collide, and still ends what was touching.
    scene.destroyEntity(successor);
    scene.destroyEntity(floor);
    tick.clear();
    step(physics, frame);
    check("an emptied world ends what touched in it", tick.ended == 1 && tick.total() == 1);
}

// A replaced world reuses slots and generations, so old contacts are not reported
// ending in the new.
void testAReplacedWorldEndsNothing() {
    std::printf("Contacts across a world replaced:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    addFallingBody(scene, {0.0f, 0.49f, 0.0f}, 0.5f);

    TestFrame frame(scene);
    ContactLog tick;
    listenToCollisions(frame.events, tick);
    PhysicsSystem physics;
    step(physics, frame);

    Scene elsewhere;
    addBox(elsewhere, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    scene.swap(elsewhere);

    tick.clear();
    step(physics, frame);
    check("the old world's contact is not reported ending", tick.ended == 0);
}

// A tick's contacts are reported by entity slot, not by position. A sweep along X meets
// these three in reverse slot order and must not report them that way.
void testContactsAreReportedInSlotOrder() {
    std::printf("The order a tick's contacts come in:\n");

    Scene scene;
    scene.physics().gravity = glm::vec3(0.0f);
    const EntityId first  = addFallingBody(scene, {1.6f, 0.0f, 0.0f}, 0.5f);
    const EntityId second = addFallingBody(scene, {0.8f, 0.0f, 0.0f}, 0.5f);
    const EntityId third  = addFallingBody(scene, {0.0f, 0.0f, 0.0f}, 0.5f);

    TestFrame frame(scene);
    ContactLog tick;
    listenToCollisions(frame.events, tick);
    PhysicsSystem physics;
    step(physics, frame);

    check("two contacts begin", tick.began == 2 && tick.collisions.size() == 2);
    check(
        "  the lower slot pair first",
        tick.collisions.size() == 2
            && tick.collisions[0].a == first
            && tick.collisions[0].b == second
            && tick.collisions[1].a == second
            && tick.collisions[1].b == third
    );
}

// Box fitting runs once, when the author asks, and produces the shipped collider
// unchecked: a bad fit is a character falling through a floor, mistaken for a
// modelling error.
void testFittingBoxesToAMesh() {
    std::printf("Boxes fitted to a mesh's volume:\n");

    const MeshAsset cube = generateCube();

    // Detail 1 is the documented degenerate case: one box, the scaled bounds.
    const std::vector<ColliderPart> single = fitBoxesToMesh(cube, 1, glm::vec3(1.0f));
    check("the coarsest fit is one box", single.size() == 1);
    check("  which is a box", !single.empty() && single[0].shape == ColliderShape::Box);
    check(
        "  spanning the mesh",
        !single.empty() && nearly(single[0].halfExtents.x, 0.5f) && nearly(single[0].halfExtents.y, 0.5f)
    );

    const std::vector<ColliderPart> fitted = fitBoxesToMesh(cube, 4, glm::vec3(1.0f));
    check("a finer fit is more than one box", fitted.size() > 1);
    bool allBoxes = true;
    glm::vec3 lo(1e9f);
    glm::vec3 hi(-1e9f);
    for (const ColliderPart& part : fitted) {
        allBoxes = allBoxes && part.shape == ColliderShape::Box;
        lo = glm::min(lo, part.center - part.halfExtents);
        hi = glm::max(hi, part.center + part.halfExtents);
    }
    check("  every part is a box, as the block says", allBoxes);
    check(
        "  and together they stay inside the mesh they approximate",
        lo.x >= -0.5001f && hi.x <= 0.5001f && lo.y >= -0.5001f && hi.y <= 0.5001f
    );

    // The solver ignores Transform scale, so the fit bakes it in.
    const std::vector<ColliderPart> scaled = fitBoxesToMesh(cube, 1, glm::vec3(2.0f, 3.0f, 4.0f));
    check(
        "the entity's scale is baked into the boxes",
        !scaled.empty()
            && nearly(scaled[0].halfExtents.x, 1.0f)
            && nearly(scaled[0].halfExtents.y, 1.5f)
            && nearly(scaled[0].halfExtents.z, 2.0f)
    );

    // Never empty: no collider at all is worse than a rough one.
    const MeshAsset empty;
    check("an empty mesh still yields a collider", !fitBoxesToMesh(empty, 8, glm::vec3(1.0f)).empty());

    MeshAsset degenerate;
    degenerate.vertices = { Vertex{{0, 0, 0}, {0, 1, 0}, {0, 0}, {1, 0, 0, 1}} };
    check(
        "  and so does one triangle short of a surface",
        !fitBoxesToMesh(degenerate, 8, glm::vec3(1.0f)).empty()
    );

    // Detail past the cap is clamped, not refused; a negative one is no grid of
    // negative cells.
    check(
        "detail past the cap is clamped, not refused",
        !fitBoxesToMesh(cube, COLLIDER_FIT_MAX_DETAIL * 4, glm::vec3(1.0f)).empty()
    );
    check("  and a nonsense detail still fits something", !fitBoxesToMesh(cube, -3, glm::vec3(1.0f)).empty());
}

} // namespace

void runPhysicsCollisionTests() {
    testCapsuleBoxFindsSmallOverlaps();
    testASunkCapsuleLeavesByItsShallowestFace();
    testGjk();
    testCollisionLayers();
    testRestOnNarrowSupport();
    testMeshCollider();
    testMeshDoesNotEjectDownward();
    testAShapeRestsOnATriangleByItsWholeFace();
    testALedgeRiserDoesNotLaunchWhatBrushesIt();
    testOffsetMeshPartCollides();
    testAMeshColliderFollowsItsMesh();
    testAMeshBuildsOneTreeEverywhere();
    testAPairIsOrderedByItsBodiesNotByWhereTheyAre();
    testStaticTriggerSeesKinematic();
    testATriggerIgnoresTheLevelItSitsIn();
    testAContactBeginsStaysAndEndsOnce();
    testATriggerReportsEnterAndExitOnce();
    testAReplayedTickReportsNothing();
    testASleepingPairIsQuietButNotEnded();
    testADestroyedBodyEndsItsContacts();
    testAReplacedWorldEndsNothing();
    testContactsAreReportedInSlotOrder();
    testFittingBoxesToAMesh();
}
