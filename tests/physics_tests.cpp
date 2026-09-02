#include "support.h"

namespace {

// A unit box centred at +5 on X and a capsule at -5, so a cast along either
// direction meets one of them square on and the other not at all.
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

    check("a capsule behind is hit when cast at",
          raycast(scene, {0,0,0}, {-1,0,0}, 100.0f, hit));
    check("  the capsule entity is reported", hit.entity == capsule);
    check("  at radius off its axis", nearly(hit.distance, 4.5f));

    // The capsule's segment is +-0.5 on Y, swept by 0.5, so it spans [-1, 1].
    check("its cap is hit from above",
          raycast(scene, {-5.0f, 5.0f, 0.0f}, {0,-1,0}, 100.0f, hit));
    check("  at the top of the sweep", nearly(hit.distance, 4.0f));
    check("a ray outside the cap radius misses",
          !raycast(scene, {-5.0f, 5.0f, 0.9f}, {0,-1,0}, 100.0f, hit));

    // A ray meeting a rotated box has to meet the rotated face. Turned 45
    // degrees about Y it presents an edge, which is nearer than the flat face
    // by exactly the difference between a half extent and its diagonal.
    Scene turned;
    const glm::quat spin = glm::angleAxis(glm::radians(45.0f), glm::vec3(0,1,0));
    addBody(turned, {5.0f, 0.0f, 0.0f}, ColliderShape::Box, spin);
    check("a rotated box is hit", raycast(turned, {0,0,0}, {1,0,0}, 100.0f, hit));
    check("  on its edge rather than its axis-aligned face",
          nearly(hit.distance, 5.0f - std::sqrt(0.5f)));
}

// The cases that decide whether a caller can trust a false: a ray pointed the
// wrong way, one that stops short, and one that starts inside.
void testRaycastMisses() {
    std::printf("Raycast - misses and degenerate input:\n");

    Scene scene;
    addBody(scene, { 5.0f, 0.0f, 0.0f}, ColliderShape::Box);
    addBody(scene, {-5.0f, 0.0f, 0.0f}, ColliderShape::Capsule);
    RayHit hit;

    check("a ray stopping short of the box misses",
          !raycast(scene, {0,0,0}, {1,0,0}, 4.0f, hit));
    check("a ray passing over everything misses",
          !raycast(scene, {0,10,0}, {1,0,0}, 100.0f, hit));

    // The capsule sits behind a ray cast along +X. Solving its side wall gives
    // an intersection at a negative distance, and reading that as a hit put a
    // phantom body at the caster's feet that won every other query.
    check("a body behind the origin is not a hit at zero",
          raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit) && hit.distance > 1.0f);

    check("a zero direction finds nothing",
          !raycast(scene, {0,0,0}, {0,0,0}, 100.0f, hit));
    check("a negative distance finds nothing",
          !raycast(scene, {0,0,0}, {1,0,0}, -1.0f, hit));

    // Callers build a direction from a velocity and a timestep without thinking
    // about it, and a slow body's is small. Rejecting those as degenerate is a
    // silent wrong answer rather than an error.
    check("half a millimetre of direction still casts",
          raycast(scene, {0,0,0}, {0.0005f, 0.0f, 0.0f}, 100.0f, hit));
    check("  and normalizes to the same answer", nearly(hit.distance, 4.5f));

    check("a ray starting inside a body reports it",
          raycast(scene, {5.0f, 0.0f, 0.0f}, {1,0,0}, 100.0f, hit));
    check("  at distance zero", nearly(hit.distance, 0.0f));
    check("  with a normal facing the caster", nearly(hit.normal.x, -1.0f));
}

void testRaycastFilters() {
    std::printf("Raycast - filters and ordering:\n");

    Scene scene;
    const EntityId far  = addBody(scene, {5.0f, 0.0f, 0.0f}, ColliderShape::Box);
    const EntityId near_ = addBody(scene, {2.0f, 0.0f, 0.0f}, ColliderShape::Box);
    RayHit hit;

    check("two bodies in line resolve to one hit",
          raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit));
    check("  the nearer one wins", hit.entity == near_);
    check("  at its distance", nearly(hit.distance, 1.5f));

    QueryFilter skipNear;
    skipNear.ignore = near_;
    check("ignore skips that entity",
          raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit, skipNear)
              && hit.entity == far);

    QueryFilter dynamicOnly;
    dynamicOnly.hitStatic = false;
    check("hitStatic false skips immovable bodies",
          !raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit, dynamicOnly));

    scene.get<Collider>(near_).isTrigger = true;
    check("a trigger is passed through by default",
          raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit) && hit.entity == far);
    QueryFilter withTriggers;
    withTriggers.hitTriggers = true;
    check("hitTriggers sees it",
          raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit, withTriggers)
              && hit.entity == near_);
    scene.get<Collider>(near_).isTrigger = false;

    scene.get<Collider>(near_).enabled = false;
    check("a disabled collider is not in the query",
          raycast(scene, {0,0,0}, {1,0,0}, 100.0f, hit) && hit.entity == far);
}

// A sweep answers a different question from a ray: not whether something is in
// the way, but where a body of a given size would come to rest.
void testSpherecast() {
    std::printf("Spherecast:\n");

    Scene scene;
    const EntityId box = addBody(scene, {5.0f, 0.0f, 0.0f}, ColliderShape::Box);
    RayHit hit;

    check("a swept sphere stops short of the face by its radius",
          spherecast(scene, {0,0,0}, 0.25f, {1,0,0}, 100.0f, hit)
              && nearly(hit.distance, 4.25f));
    check("  and reports the surface, not its own centre",
          nearly(hit.point.x, 4.5f));
    check("  the box entity is reported", hit.entity == box);

    // The ray passes over the box; the sphere is wide enough to catch its edge.
    // This is the whole reason the query exists.
    check("a ray at 0.6 clears a box of half extent 0.5",
          !raycast(scene, {0.0f, 0.6f, 0.0f}, {1,0,0}, 100.0f, hit));
    check("a sphere of 0.25 at the same height does not",
          spherecast(scene, {0.0f, 0.6f, 0.0f}, 0.25f, {1,0,0}, 100.0f, hit));
    check("  stopping on the edge it clips",
          nearly(hit.distance, 4.27087f));

    // Aimed at a corner, where a slab test alone would stop the sphere on a
    // face plane it never reaches.
    Scene corner;
    addBody(corner, {0.0f, 0.0f, 0.0f}, ColliderShape::Box);
    const float diagonal = std::sqrt(3.0f * 4.5f * 4.5f);
    check("a sweep into a corner stops on the corner",
          spherecast(corner, {5,5,5}, 0.25f, {-1,-1,-1}, 100.0f, hit)
              && nearly(hit.distance, diagonal - 0.25f));

    Scene capsule;
    addBody(capsule, {-5.0f, 0.0f, 0.0f}, ColliderShape::Capsule);
    check("a swept sphere stops a combined radius from a capsule axis",
          spherecast(capsule, {0,0,0}, 0.25f, {-1,0,0}, 100.0f, hit)
              && nearly(hit.distance, 4.25f));

    check("a sweep starting overlapped stops where it started",
          spherecast(scene, {5.0f, 0.0f, 0.0f}, 0.25f, {1,0,0}, 100.0f, hit)
              && nearly(hit.distance, 0.0f));
    check("a sweep away from everything misses",
          !spherecast(scene, {0,0,0}, 0.25f, {-1,0,0}, 100.0f, hit));
    check("a sweep stopping short misses",
          !spherecast(scene, {0,0,0}, 0.25f, {1,0,0}, 4.0f, hit));

    // A radius of zero is a ray, and answering it twice invites the two
    // answers disagreeing at the edges.
    RayHit asRay;
    raycast(scene, {0,0,0}, {1,0,0}, 100.0f, asRay);
    check("a zero radius defers to raycast",
          spherecast(scene, {0,0,0}, 0.0f, {1,0,0}, 100.0f, hit)
              && nearly(hit.distance, asRay.distance));
}

// GJK has to agree with the routine the engine already trusts. Box against box
// is the one pair both can answer, so it is the one that can be checked against
// something other than my own arithmetic.
// The segment-to-box closest point used to be found by projecting back and
// forth between the segment and the box. Alternating projection between two
// convex sets has fixed points that are not the nearest pair, so it settled on
// them and reported real overlaps as no contact at all - a capsule passing
// through geometry it was touching. These three are cases it missed, found by
// checking it against a densely sampled exact distance; the overlaps are two
// millimetres, four, and eleven.
void testCapsuleBoxFindsSmallOverlaps() {
    std::printf("Capsule against box, barely touching:\n");

    struct Case { glm::vec3 half; glm::vec3 a; glm::vec3 b; float radius; };
    const Case missedBefore[] = {
        {{1.070485f, 1.059132f, 1.263437f},
         {-1.577023f, -2.536281f, 2.629774f}, {-1.436692f, 0.909800f, -0.901492f}, 0.368448f},
        {{1.454872f, 0.293407f, 1.480604f},
         {-1.342335f, 2.620376f, 2.097023f}, {-1.542812f, -0.073972f, 0.878635f}, 0.064073f},
        {{0.692265f, 1.117279f, 0.760827f},
         {0.963844f, 0.254541f, -2.078951f}, {0.781100f, 1.121073f, 2.809511f}, 0.176733f},
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

    // And it still says no when there is genuinely nothing there.
    BoxShape unit;
    unit.center = {0.0f, 0.0f, 0.0f};
    unit.halfExtents = {0.5f, 0.5f, 0.5f};
    CapsuleShape clear;
    clear.a = {3.0f, 0.0f, 0.0f};
    clear.b = {3.0f, 1.0f, 0.0f};
    clear.radius = 0.2f;
    Contact none[MAX_CONTACTS_PER_MANIFOLD];
    check("  and one well clear of it is not",
          contactCapsuleBox(clear, unit, none) == 0);
}

void testGjk() {
    std::printf("GJK / EPA:\n");

    const BoxShape unit = boxAt({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f});

    const glm::vec3 half = {0.5f, 0.5f, 0.5f};
    check("boxes clear of each other do not overlap",
          !gjkOverlap(supportOf(unit), supportOf(boxAt({2.0f, 0, 0}, half))));
    check("boxes sharing space do",
          gjkOverlap(supportOf(unit), supportOf(boxAt({0.5f, 0, 0}, half))));

    // Overlapping by 0.2 along X: the shallowest way apart is along X, by 0.2.
    const BoxShape near_ = boxAt({0.8f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f});
    Contact gjk;
    check("EPA finds the contact",
          gjkContact(supportOf(unit), supportOf(near_), gjk));
    check("  along the shallowest axis",
          std::fabs(std::fabs(gjk.normal.x) - 1.0f) < 0.01f);
    check("  at the depth they share", nearly(gjk.penetration, 0.2f));

    // And the same answer the separating-axis routine gives, which is the whole
    // reason to trust it on shapes that routine cannot handle.
    Contact sat[MAX_CONTACTS_PER_MANIFOLD];
    const int count = contactBoxes(unit, near_, sat);
    check("the SAT routine agrees there is a contact", count > 0);
    if (count > 0) {
        check("  on the same axis",
              std::fabs(std::fabs(sat[0].normal.x) - 1.0f) < 0.01f);
        check("  to the same depth",
              std::fabs(sat[0].penetration - gjk.penetration) < 0.02f);
    }

    std::printf("GJK - shapes SAT cannot pair:\n");

    CapsuleShape capsule;
    capsule.a = {0.0f, -0.4f, 0.0f};
    capsule.b = {0.0f,  0.4f, 0.0f};
    capsule.radius = 0.3f;
    check("a capsule through a box overlaps",
          gjkOverlap(supportOf(unit), supportOf(capsule)));

    CapsuleShape sphere;
    sphere.a = {1.6f, 0.0f, 0.0f};
    sphere.b = sphere.a;
    sphere.radius = 0.3f;
    check("a sphere clear of it does not",
          !gjkOverlap(supportOf(unit), supportOf(sphere)));
    sphere.a = {0.7f, 0.0f, 0.0f};
    sphere.b = sphere.a;
    check("a sphere touching it does",
          gjkOverlap(supportOf(unit), supportOf(sphere)));

    // A point cloud is its own hull as far as a support query is concerned,
    // which is what lets a mesh triangle use this path without a routine of
    // its own.
    const glm::vec3 tetra[4] = {
        {0.4f, 0.0f, 0.0f}, {1.4f, 0.0f, 0.0f},
        {0.9f, 1.0f, 0.0f}, {0.9f, 0.5f, 1.0f}
    };
    check("a point cloud overlapping the box does",
          gjkOverlap(supportOf(unit), supportOfPoints(tetra, 4)));

    const glm::vec3 far_[3] = {
        {3.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {3.5f, 1.0f, 0.0f}
    };
    check("a triangle well clear of it does not",
          !gjkOverlap(supportOf(unit), supportOfPoints(far_, 3)));

    // Concentric: the origin is inside the difference from the first step, which
    // is where a search that assumes it starts outside falls over.
    check("boxes exactly on top of each other overlap",
          gjkOverlap(supportOf(unit), supportOf(unit)));
    Contact deep;
    const bool escaped = gjkContact(supportOf(unit), supportOf(unit), deep);
    check("  and EPA still names a way out", escaped && deep.penetration > 0.9f);
}

// A query has to know what it is looking at. Every shape was tested as a box or
// a capsule before this: anything that was not a box got the capsule path and a
// phantom capsule of whatever radius the part happened to carry, so a sweep at
// a mesh floor missed a floor that was there.
void testQueryAgainstNewShapes() {
    std::printf("Queries against meshes:\n");

    RayHit hit;

    // And the case step-up depends on: probing a triangle floor.
    Scene terrain;
    addMeshBody(terrain, makeGridMesh(16, 16.0f));

    check("a ray finds a triangle mesh below it",
          raycast(terrain, {3.0f, 4.0f, -2.0f}, {0,-1,0}, 100.0f, hit));
    check("  at the surface", nearly(hit.distance, 4.0f));
    check("  facing up out of it", hit.normal.y > 0.9f);

    check("a sweep finds it too, a radius short",
          spherecast(terrain, {3.0f, 4.0f, -2.0f}, 0.5f, {0,-1,0}, 100.0f, hit)
              && nearly(hit.distance, 3.5f));

    check("a ray past the edge of the mesh finds nothing",
          !raycast(terrain, {20.0f, 4.0f, 0.0f}, {0,-1,0}, 100.0f, hit));
}

// Two things can share a world without touching, which is what a layer says.
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
    check("a body falls through what its mask excludes",
          ignored.get<Transform>(ghost).position.y < -2.0f);

    // The mask is read both ways: excluding from either side is enough, or
    // "does A hit B" would depend on which was asked.
    Scene oneSided;
    const EntityId floor2 =
        addBox(oneSided, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    oneSided.get<Rigidbody>(floor2).layer = LEVEL;
    oneSided.get<Rigidbody>(floor2).collidesWith = ~BONES;

    const EntityId falling = addFallingBody(oneSided, {0.0f, 3.0f, 0.0f}, 0.5f);
    oneSided.get<Rigidbody>(falling).layer = BONES;

    simulate(oneSided, 180);
    check("  and excluding from one side is enough",
          oneSided.get<Transform>(falling).position.y < -2.0f);

    // Unset, everything collides with everything, so nothing that never heard
    // of a layer changes behaviour.
    Scene plain;
    addBox(plain, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId lands = addFallingBody(plain, {0.0f, 3.0f, 0.0f}, 0.5f);
    simulate(plain, 180);
    check("  while the defaults collide with everything",
          plain.get<Transform>(lands).position.y > 0.0f);

    // A query takes the same mask, so "the level, not the characters" is a
    // thing to ask for rather than a list of entities to ignore.
    Scene mixed;
    const EntityId ground =
        addBox(mixed, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    mixed.get<Rigidbody>(ground).layer = LEVEL;
    const EntityId limb = addBox(mixed, {0.0f, 1.0f, 0.0f}, {0.4f, 0.4f, 0.4f});
    mixed.get<Rigidbody>(limb).layer = BONES;

    RayHit hit;
    check("a ray with no mask hits whatever is nearest",
          raycast(mixed, {0.0f, 5.0f, 0.0f}, {0,-1,0}, 20.0f, hit)
              && hit.entity == limb);

    QueryFilter levelOnly;
    levelOnly.layerMask = LEVEL;
    check("  and a masked one looks past what it excludes",
          raycast(mixed, {0.0f, 5.0f, 0.0f}, {0,-1,0}, 20.0f, hit, levelOnly)
              && hit.entity == ground);

    // What the ragdoll build does with them: bones on their own layer, and the
    // owner's mask cleared of it, so a rig is never pushed by its own limbs.
    Scene rigged;
    const EntityId owner = addFallingBody(rigged, {0.0f, 1.0f, 0.0f}, 0.3f);
    const uint32_t built = buildRagdoll(rigged, owner, makeTestRig());
    check("a ragdoll puts its bones on their own layer", built == 4);
    const int boneLayer = RagdollSettings{}.boneLayer;
    const int ownerMask = rigged.get<Rigidbody>(owner).collidesWith;
    check("  and takes that layer out of the owner's mask",
          (ownerMask & boneLayer) == 0);
    check("  leaving the owner colliding with everything else",
          (ownerMask & ~boneLayer) == ~boneLayer);

    // And the bones do not hit each other. Each capsule spans its bone to that
    // bone's child, so limbs are built overlapping; a rig that self-collides
    // spends its first tick resolving interpenetration it was authored with and
    // throws itself apart. A joint already spares adjacent bones - this is what
    // spares one thigh from the other.
    bool anyBoneSelfCollides = false;
    for (const RagdollBone& bone : rigged.get<Ragdoll>(owner).bones) {
        const Rigidbody& body = rigged.get<Rigidbody>(bone.body);
        if ((body.layer & body.collidesWith) != 0) anyBoneSelfCollides = true;
    }
    check("  and the bones do not collide with each other", !anyBoneSelfCollides);

    // And gives it back. collidesWith is authored, serialized and visible, so a
    // character that once had a ragdoll must not quietly stop colliding with a
    // layer it was never told about.
    clearRagdoll(rigged, owner);
    check("  and clearing the ragdoll returns the layer to the owner",
          (rigged.get<Rigidbody>(owner).collidesWith & boneLayer) == boneLayer);
}

// A body resting on a support narrower than itself. Two horizontal edges cross
// to a vertical axis that duplicates the face normal, and under a whisker of
// tilt float noise can hand the SAT that duplicate: the four-point face
// manifold becomes one corner, and the position correction rocks the body on it.
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
    check("a plank on a narrow fulcrum comes to rest",
          glm::length(out.linearVelocity) < 0.01f
       && glm::length(out.angularVelocity) < 0.05f);
    check("  on top of it, not sunk into it", restY > 0.70f);
    check("  and sleeps there", out.sleeping);
}

void testMeshCollider() {
    std::printf("Mesh collider:\n");

    const MeshAsset grid = makeGridMesh(16, 16.0f);

    Collider probe;
    probe.parts.clear();
    const uint32_t triangles = addMeshCollider(probe, grid);
    check("a mesh becomes triangles", triangles == 16 * 16 * 2);
    check("  stored three points each",
          probe.meshPoints.size() == triangles * 3);
    check("  with a hierarchy over them", !probe.meshNodes.empty());

    // The tree has to cull, or it is a list with extra steps. A metre-wide
    // query over a sixteen-metre floor should reach a handful of triangles.
    std::vector<uint32_t> found;
    queryMeshBvh(probe.meshNodes, {-0.5f, -1.0f, -0.5f}, {0.5f, 1.0f, 0.5f},
                 found);
    check("  that culls rather than returning everything",
          !found.empty() && found.size() < triangles / 4);

    // A far-away query reaches nothing at all.
    found.clear();
    queryMeshBvh(probe.meshNodes, {100.0f, 0.0f, 100.0f},
                 {101.0f, 1.0f, 101.0f}, found);
    check("  and finds nothing where there is nothing", found.empty());

    // Now simulate against it. A body falling onto a triangle floor must stop
    // on it, which is the whole pairing: broadphase, tree walk, then GJK per
    // triangle it hands back.
    Scene scene;
    addMeshBody(scene, grid);
    const EntityId box = addFallingBody(scene, {0.0f, 3.0f, 0.0f}, 0.5f);

    simulate(scene, 240);
    const glm::vec3 rest = scene.get<Transform>(box).position;
    check("a body lands on a triangle mesh", rest.y > 0.25f && rest.y < 1.0f);
    check("  without sliding off it",
          glm::length(glm::vec2(rest.x, rest.z)) < 1.0f);

    // Away from the origin, which is the case a mesh exists for: a floor is
    // sixteen metres across and a body lands wherever it lands. Passing this
    // only above the origin means passing it by where the test put the box.
    Scene offCentre;
    addMeshBody(offCentre, grid);
    const EntityId away = addFallingBody(offCentre, {5.0f, 3.0f, -4.0f}, 0.5f);
    simulate(offCentre, 240);
    check("  and lands away from the origin too",
          offCentre.get<Transform>(away).position.y > 0.25f);

    // Off the edge of the mesh there is nothing to land on, and a body there
    // should fall past rather than be caught by a triangle it never met.
    Scene edge;
    addMeshBody(edge, grid);
    const EntityId beyond = addFallingBody(edge, {20.0f, 3.0f, 0.0f}, 0.5f);
    simulate(edge, 180);
    check("  and falls past where the mesh is not",
          edge.get<Transform>(beyond).position.y < -1.0f);
}

// A mesh part with a centre of its own. The tree is built over the raw points,
// so the query bound and the triangle placement have to come back to that space
// through the same two terms - the body's pose and the part's centre. Applying
// one to the placement and not the other to the bound offsets them by exactly
// the centre, and a slab test that misses returns no candidate, so the body
// falls through a floor that is right there.
// A triangle is a zero-thickness hull, so its Minkowski difference with a body
// is symmetric about its plane: the shallowest way out flips the moment the
// body's centre crosses it, and the position pass then drives the body down
// through the floor rather than back up onto it. Started already sunk, which is
// the state a fast body or a bad tick arrives in.
void testMeshDoesNotEjectDownward() {
    std::printf("A body sunk into a mesh floor:\n");

    Scene scene;
    addMeshBody(scene, makeGridMesh(8, 12.0f));

    // Centre below the plane, which is where the normal used to flip.
    const EntityId sunk = addFallingBody(scene, {0.5f, -0.12f, 0.5f}, 0.5f);
    simulate(scene, 300);

    const float y = scene.get<Transform>(sunk).position.y;
    check("it is pushed back up, not through", y > 0.0f);
    check("  and comes to rest on the surface", y > 0.25f && y < 1.0f);
}

void testOffsetMeshPartCollides() {
    std::printf("A mesh part offset from its entity:\n");

    Scene scene;
    const MeshAsset grid = makeGridMesh(8, 12.0f);

    const EntityId floorId = scene.createEntity();
    scene.add<Transform>(floorId, Transform{});
    Rigidbody floorBody;
    floorBody.isStatic = true;
    scene.add<Rigidbody>(floorId, std::move(floorBody));

    Collider collider;
    collider.parts.clear();
    check("a mesh to stand on", addMeshCollider(collider, grid) > 0);
    // Shifted two metres up: the triangles are at y = 0 in their own points,
    // so the surface they describe now sits at y = 2.
    collider.parts[0].center = {0.0f, 2.0f, 0.0f};
    scene.add<Collider>(floorId, std::move(collider));

    const EntityId faller = addFallingBody(scene, {0.5f, 6.0f, 0.5f}, 0.4f);
    simulate(scene, 400);

    const float y = scene.get<Transform>(faller).position.y;
    check("  a body lands on it where the offset puts it", y > 2.0f && y < 3.0f);
    check("  rather than falling through it", y > 0.0f);
}

// Every collider in a loaded scene is offered to rebuildMeshBvh, and almost
// none of them have a mesh part, so the bounds check must prove the part exists
// before reading it.
void testRebuildMeshBvhWithoutMesh() {
    std::printf("Rebuilding a tree that has no mesh:\n");

    Collider box;
    ColliderPart part;
    part.shape = ColliderShape::Box;
    part.halfExtents = {0.5f, 0.5f, 0.5f};
    box.parts = { part };
    rebuildMeshBvh(box);
    check("a box collider survives being offered a tree rebuild",
          box.meshNodes.empty() && box.parts.size() == 1);

    Collider empty;
    empty.parts.clear();
    rebuildMeshBvh(empty);
    check("  and so does one with no parts at all", empty.meshNodes.empty());

    // A part whose span runs past the buffer: the case the bounds check is for.
    Collider ragged;
    ragged.meshPoints = {{0,0,0}, {1,0,0}, {0,1,0}};
    ColliderPart lying;
    lying.shape = ColliderShape::Mesh;
    lying.meshFirst = 0;
    lying.meshCount = 30;          // three points, thirty claimed
    ragged.parts = { lying };
    rebuildMeshBvh(ragged);
    check("  and a part claiming more points than exist builds no tree",
          ragged.meshNodes.empty());
}

void testJoints() {
    std::printf("Joints:\n");

    // A point joint to a fixed anchor. Gravity pulls for three seconds and the
    // joint is the only thing arguing.
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
    check("a point joint holds a body against gravity",
          std::fabs(held.y - 5.0f) < 0.25f);
    check("  at the anchor, not merely near it",
          glm::length(held - glm::vec3(0.0f, 5.0f, 0.0f)) < 0.25f);

    // Without the joint the same body is on the floor by now, which is what
    // says the joint did the holding rather than the test being generous.
    Scene loose;
    addAnchor(loose, {0.0f, 5.0f, 0.0f});
    const EntityId dropped = addFallingBody(loose, {0.0f, 4.0f, 0.0f}, 0.25f);
    simulate(loose, 180);
    check("the same body without one falls",
          loose.get<Transform>(dropped).position.y < 0.0f);

    // The documented world pin: connected has a pose but no Rigidbody, and the
    // joint holds to that point in the world. It used to be silently dropped -
    // the contract lived on the component and nowhere in the solver.
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
    check("a joint holds to an entity that has no body of its own",
          glm::length(pinned.get<Transform>(strung).position
                      - glm::vec3(0.0f, 5.0f, 0.0f)) < 0.25f);

    // A distance joint leaves everything but the distance free, so a body hung
    // off-centre swings down and ends below the anchor at the length it was
    // given rather than at the point it started from.
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

    // Sampled throughout rather than at the end. A pendulum with nothing
    // damping it never stops, so the body's height at any one tick is a phase
    // of the swing and says nothing - it was asserted once and passed by luck.
    float lowest = 5.0f;
    float worstSpan = 0.0f;
    for (int step = 0; step < 30; ++step) {
        simulate(rope, 10);
        const glm::vec3 at = rope.get<Transform>(bob).position;
        lowest = glm::min(lowest, at.y);
        const float span = glm::length(at - glm::vec3(0.0f, 5.0f, 0.0f));
        worstSpan = glm::max(worstSpan, std::fabs(span - 2.0f));
    }
    check("a distance joint keeps its length through the swing",
          worstSpan < 0.3f);
    check("  while letting the body swing", lowest < 4.0f);
}

// A jointed pair generates no manifold on purpose, and the only thing that woke
// a sleeper walked manifolds - so a body asleep on the end of a joint was a
// nail. The solver treats a sleeper as immovable, so pulling the other end did
// nothing for as long as the pull lasted.
// Stiffness used to scale each solver pass's impulse, so the error decayed by
// (1 - stiffness) per pass and what was delivered was 1 - (1 - stiffness)^n.
// At the default eight passes half stiffness was 99.6% of rigid, and the knob
// silently changed meaning with a scene-wide solver setting.
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

        // Few ticks on purpose: what differs is the rate the gap closes at,
        // and after enough ticks every rate has closed it.
        simulate(scene, 3);
        return scene.get<Transform>(body).position.x - 1.0f;   // gap left
    };

    const float few  = driftAfter(0.25f, 2);
    const float many = driftAfter(0.25f, 16);
    check("half-closed at two passes and at sixteen agree",
          std::fabs(few - many) < 0.05f);

    // And it still means something: a stiffer joint closes more of the gap.
    check("  and a stiffer joint closes more of the gap",
          driftAfter(0.8f, 8) < driftAfter(0.2f, 8) - 0.05f);
}

void testJointWakesASleeper() {
    std::printf("A sleeper on the end of a joint:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});

    // Resting on the floor and asleep, which is where a body ends up.
    const EntityId anchorBody = addFallingBody(scene, {0.0f, 0.4f, 0.0f}, 0.4f);
    const EntityId hauler = addFallingBody(scene, {2.0f, 0.4f, 0.0f}, 0.4f);

    Joint rope;
    rope.type = JointType::Distance;
    rope.connected = anchorBody;
    rope.distance = 2.0f;
    scene.add<Joint>(hauler, std::move(rope));

    simulate(scene, 240);

    // Put it to sleep rather than wait for it: a jointed body is nudged by its
    // own constraint every tick, so it settles slowly and what is under test is
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

// The joint contract says `connected` may name an entity with no Rigidbody, and
// that the joint then holds the body to a fixed point in the world. Combined
// with an unset distance - "whatever they were apart on the first tick" - that
// path read a map iterator the branch above had already proved to be end().
void testDistanceJointPinnedToWorld() {
    std::printf("A rope pinned to a world point:\n");

    Scene scene;
    // The anchor: a pose and nothing else, which is what makes it a world pin.
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
    check("a weight on an auto-length rope hangs where it started",
          std::fabs(y - 1.5f) < 0.15f);
    check("  and the length it measured is recorded",
          std::fabs(scene.get<Joint>(weight).resolvedDistance - 2.5f) < 0.05f);
    // The request outlives the answer: the measurement lands in
    // resolvedDistance, so a saved scene still asks to be measured rather than
    // carrying a length nobody chose.
    check("  while the authored field still asks to be measured",
          scene.get<Joint>(weight).distance < 0.0f);
}

// A prefab carrying a joint. Its references were saved as raw scene slots, so
// an instance in any other scene pointed its joint at whatever those slots
// happened to hold there - a wrecking ball prefab tied to a wall, a crate, or
// nothing, depending on load order.
void testPrefabKeepsItsJoints() {
    std::printf("Prefab round-trip of a joint:\n");

    Scene authored;
    const EntityId anchor = authored.createEntity();
    {
        Transform t;
        t.position = {0.0f, 5.0f, 0.0f};
        authored.add<Transform>(anchor, std::move(t));
        Rigidbody rb;
        rb.isStatic = true;
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
        (std::filesystem::temp_directory_path() / "vkm_joint_prefab.json").string();
    check("the prefab saves", Prefab::save(authored, anchor, path, resources));

    // A scene whose slots are already taken, so a raw slot reference would
    // land on one of these instead of on the instance's own anchor.
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
    check("  tied to the instance's own anchor, not a slot",
          hung && target.get<Joint>(hung).connected == instance);

    std::filesystem::remove(path);
}

void testRagdoll() {
    std::printf("Ragdoll:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});

    const EntityId rig = scene.createEntity();
    scene.add<Transform>(rig, Transform{});

    const SkeletonAsset skeleton = makeTestRig();
    const uint32_t built = buildRagdoll(scene, rig, skeleton);

    // A limb spans a bone to its first child, so the three bones with no child
    // - the chest and both feet - are tips and get nothing.
    check("a ragdoll is built from the rig", built == 4);
    check("  and the component records it",
          scene.has<Ragdoll>(rig) && scene.get<Ragdoll>(rig).bones.size() == 4);

    // Every simulated bone got a body with a capsule and a mass.
    bool wellFormed = true;
    float totalMass = 0.0f;
    for (const RagdollBone& bone : scene.get<Ragdoll>(rig).bones) {
        if (!scene.has<Collider>(bone.body) || !scene.has<Rigidbody>(bone.body)) {
            wellFormed = false;
            continue;
        }
        const Collider& collider = scene.get<Collider>(bone.body);
        if (collider.parts.empty()
                || collider.parts[0].shape != ColliderShape::Capsule) {
            wellFormed = false;
        }
        totalMass += scene.get<Rigidbody>(bone.body).mass;
    }
    check("  every bone got a capsule and a mass", wellFormed);
    check("  sharing out the total rather than each taking it",
          std::fabs(totalMass - 70.0f) < 1.0f);

    // Three joints for four bodies: the hips are the root and hang from nothing.
    int joints = 0;
    for (const RagdollBone& bone : scene.get<Ragdoll>(rig).bones) {
        if (scene.has<Joint>(bone.body)) ++joints;
    }
    check("  and a joint for every bone but the root", joints == 3);

    // Now let it fall. Held together, it lands as a heap on the floor; not held
    // together, the pieces keep whatever velocity they had and scatter.
    const EntityId hipBody = scene.get<Ragdoll>(rig).bones[0].body;
    const glm::vec3 hips = scene.get<Transform>(hipBody).position;
    simulate(scene, 240);

    float lowest = 100.0f;
    float highest = -100.0f;
    float spread = 0.0f;
    for (const RagdollBone& bone : scene.get<Ragdoll>(rig).bones) {
        const glm::vec3 at = scene.get<Transform>(bone.body).position;
        lowest = glm::min(lowest, at.y);
        highest = glm::max(highest, at.y);
        spread = glm::max(spread, glm::length(glm::vec2(at.x, at.z)));
    }

    check("it falls", lowest < hips.y);
    check("  and stops on the floor rather than through it", lowest > -1.0f);
    check("  landing as one body, not several", spread < 2.0f);
    check("  with nothing left standing", highest < 1.5f);

    // Clearing takes the bodies with it: a rebuilt ragdoll must not leave a
    // second skeleton lying in the scene.
    const size_t before = scene.entityCount();
    clearRagdoll(scene, rig);
    check("clearing a ragdoll destroys its bodies",
          !scene.has<Ragdoll>(rig) && scene.entityCount() == before - 5);

    // And so does destroying the character. The hierarchy takes the group node
    // and the bones under it, but a bone that something moved out of the group
    // is not covered by that, which is what the observer is for - so this has
    // to hold whether or not the two ever disagree.
    Scene owned;
    const EntityId doomed = owned.createEntity();
    owned.add<Transform>(doomed, Transform{});
    buildRagdoll(owned, doomed, skeleton);

    RagdollSystem lifetime;
    ResourceManager ownedRes;
    Clock ownedClock;
    EventBus ownedEvents;
    WindowManager ownedWindow;
    InputMap ownedInput;
    NetSession ownedNet;
    FrameContext ownedCtx{owned, ownedRes, ownedClock, ownedEvents,
                          ownedWindow, ownedInput, ownedNet};
    lifetime.init(ownedCtx);

    const size_t withRagdoll = owned.entityCount();
    check("a built ragdoll adds its bones to the scene", withRagdoll == 6);
    owned.destroyEntity(doomed);
    check("destroying the character takes its bones with it",
          owned.entityCount() == 0);
    lifetime.shutdown();
}

// Dying twice. A ragdoll that has come to rest is asleep where it landed, and
// going inactive makes the bones kinematic - which skips the sleep test rather
// than clearing it. Handing them back to the solver still asleep hands it
// bodies isFrozen treats as immovable, so the second death never falls.
void testRagdollFallsTwice() {
    std::printf("A ragdoll that falls twice:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId rig = scene.createEntity();
    Transform at;
    at.position = {0.0f, 3.0f, 0.0f};
    scene.add<Transform>(rig, std::move(at));
    check("a rig to knock down", buildRagdoll(scene, rig, makeTestRig()) == 4);

    RagdollSystem ragdolls;
    ResourceManager resources;
    Clock clock;
    EventBus events;
    WindowManager window;
    InputMap input;
    NetSession net;
    FrameContext ctx{scene, resources, clock, events, window, input, net};
    ragdolls.init(ctx);

    const EntityId hips = scene.get<Ragdoll>(rig).bones[0].body;

    // The state a rested ragdoll is in, set rather than waited for: what is
    // under test is what activation does about it, not how long resting takes.
    for (const RagdollBone& bone : scene.get<Ragdoll>(rig).bones) {
        Rigidbody& body = scene.get<Rigidbody>(bone.body);
        body.sleeping = true;
        body.isKinematic = true;
    }
    scene.get<Ragdoll>(rig).active = true;
    ragdolls.fixedUpdate(ctx);

    check("activating a rested ragdoll wakes its bones",
          !scene.get<Rigidbody>(hips).sleeping
       && !scene.get<Rigidbody>(hips).isKinematic);

    const float before =
        HierarchyOperations::computeWorldMatrix(scene, hips)[3][1];
    for (int i = 0; i < 200; ++i) {
        ragdolls.fixedUpdate(ctx);
        simulate(scene, 1);
    }
    const float after =
        HierarchyOperations::computeWorldMatrix(scene, hips)[3][1];
    check("  so it falls rather than hanging where it slept", after < before - 0.5f);

    ragdolls.shutdown();
}

// The half of a ragdoll that is on screen. Every other ragdoll assertion checks
// where the bodies went; this checks that the rig follows them, which is what
// anyone actually sees - and was the one path in the feature with no test.
void testRagdollPose() {
    std::printf("Ragdoll pose:\n");

    Scene scene;
    const EntityId rig = scene.createEntity();
    scene.add<Transform>(rig, Transform{});

    const SkeletonAsset skeleton = makeTestRig();
    check("a ragdoll to pose from", buildRagdoll(scene, rig, skeleton) == 4);

    const Ragdoll& ragdoll = scene.get<Ragdoll>(rig);
    const uint32_t bones = static_cast<uint32_t>(skeleton.bones.size());

    PoseBuffer poses;
    poses.clear();
    const uint32_t slice = poses.addSlice(bones);

    // Composed where the build left the bodies, so every bone should land back
    // on its own bind position: the offsets recorded at build are exactly what
    // undoes the difference between a limb's middle and its head.
    composeRagdollPose(ragdoll, gatherRagdollBodies(scene, ragdoll),
                       skeleton, glm::mat4(1.0f),
                       poses.writeTo(slice));

    const std::vector<glm::mat4>& global = poses.global();
    // hips sits a metre up in the rig, and its children hang below it.
    check("a simulated bone poses where it was built",
          nearly(global[slice + 0][3][1], 1.0f));
    check("  and a bone with no body of its own follows its parent",
          nearly(global[slice + 2][3][1], 1.6f));   // chest: hips + spine + chest

    // Now move every body a known distance. A pose that reads the bodies moves
    // with them; one that quietly fell back to the bind pose does not, and
    // would have passed every assertion above.
    constexpr float SHIFT = 5.0f;
    for (const RagdollBone& bone : ragdoll.bones) {
        scene.get<Transform>(bone.body).position.x += SHIFT;
    }
    composeRagdollPose(ragdoll, gatherRagdollBodies(scene, ragdoll),
                       skeleton, glm::mat4(1.0f),
                       poses.writeTo(slice));

    check("moving the bodies moves the bones with them",
          nearly(global[slice + 0][3][0], SHIFT));
    check("  carrying the bones that have no body along",
          nearly(global[slice + 2][3][0], SHIFT));

    // The rig's own transform is the frame the pose is in, so posing through a
    // moved rig has to take it back out - or a character walks away from its
    // own skeleton at twice the speed.
    const glm::mat4 rigWorld =
        glm::translate(glm::mat4(1.0f), {SHIFT, 0.0f, 0.0f});
    composeRagdollPose(ragdoll, gatherRagdollBodies(scene, ragdoll),
                       skeleton, rigWorld, poses.writeTo(slice));
    check("the pose is relative to the rig, not the world",
          nearly(global[slice + 0][3][0], 0.0f));

    // The bound the visibility pass sizes a skinned mesh from. A composer that
    // writes poses and no bound leaves whatever the last writer left, and a
    // ragdoll culled by its own stale bound stops being drawn at exactly the
    // moment it starts moving.
    const PoseSlice& bound = poses.slices().front();
    check("the pose publishes a bound the visibility pass can use",
          bound.originMax.y > bound.originMin.y && bound.maxBoneScale >= 1.0f);

    // The palette is what the vertex shader reads, and a pose written without
    // one draws a character in its bind shape however the bones moved.
    const std::vector<glm::mat4>& palette = poses.palette();
    check("the skinning palette is written beside the pose",
          palette.size() == global.size()
       && nearly(palette[slice + 0][3][1], global[slice + 0][3][1]));

    // A bone whose body was destroyed - a ragdoll partly cleared, or a scene
    // that loaded one body short - still has to be posed rather than left as
    // whatever the buffer held.
    Scene broken;
    const EntityId partial = broken.createEntity();
    broken.add<Transform>(partial, Transform{});
    buildRagdoll(broken, partial, skeleton);
    broken.destroyEntity(broken.get<Ragdoll>(partial).bones[1].body);

    PoseBuffer second;
    second.clear();
    const uint32_t slice2 = second.addSlice(bones);
    composeRagdollPose(broken.get<Ragdoll>(partial),
                       gatherRagdollBodies(broken, broken.get<Ragdoll>(partial)),
                       skeleton,
                       glm::mat4(1.0f), second.writeTo(slice2));
    // Where the fallback puts it, not merely that it is a number: the buffer is
    // zero-filled, so isfinite() held for a bone the composer never touched.
    check("a bone whose body is gone falls back to its parent",
          nearly(second.global()[slice2 + 1][3][1], 1.3f));
}

void testRagdollAsHitboxes() {
    std::printf("A ragdoll as hit boxes:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});

    const EntityId character = addFallingBody(scene, {0.0f, 1.0f, 0.0f}, 0.3f);
    scene.get<Rigidbody>(character).freezeRotation = true;
    const SkeletonAsset skeleton = makeTestRig();
    check("a rig to shoot at", buildRagdoll(scene, character, skeleton) == 4);

    // Held to the pose rather than falling, which is what an inactive ragdoll
    // is for: the bones are where the animation put them.
    RagdollSystem ragdolls;
    ResourceManager resources;
    Clock clock;
    EventBus events;
    WindowManager window;
    InputMap input;
    NetSession net;
    FrameContext ctx{scene, resources, clock, events, window, input, net};
    ragdolls.fixedUpdate(ctx);

    // Asked for limbs, because the character's own collider is in the way of an
    // unfiltered ray - which is itself the reason the bones are on their own
    // layer, and the reason a game asks this way round.
    QueryFilter limbs;
    limbs.layerMask = RagdollSettings{}.boneLayer;

    RayHit hit;
    check("a ray finds a limb",
          raycast(scene, {5.0f, 1.3f, 0.0f}, {-1,0,0}, 20.0f, hit, limbs));

    int32_t boneIndex = -1;
    const EntityId owner = ragdollOwnerOf(scene, hit.entity, &boneIndex);
    check("  and the limb says whose it is", owner == character);
    const int32_t boneCount = static_cast<int32_t>(skeleton.bones.size());
    check("  and which bone it was", boneIndex >= 0 && boneIndex < boneCount);

    // Which is what makes it usable: the bone's name is the rig's own.
    if (boneIndex >= 0) {
        std::printf("      (it was '%s')\n",
                    skeleton.bones[static_cast<size_t>(boneIndex)].name.c_str());
    }

    // Asking about something that is not a limb answers nothing rather than
    // guessing.
    check("something that is not a limb has no owner",
          !ragdollOwnerOf(scene, character, nullptr));

    // The same ray with the bones masked out. Asserted as two facts rather than
    // as "it missed, or what it found was not a bone": that disjunction was
    // satisfied by the miss alone, and a mask that excluded nothing would have
    // passed it just as well.
    QueryFilter withoutBones;
    withoutBones.layerMask = ~RagdollSettings{}.boneLayer;
    RayHit other;
    const bool foundSomething =
        raycast(scene, {5.0f, 1.3f, 0.0f}, {-1,0,0}, 20.0f, other, withoutBones);
    check("  the same ray without them still finds the character", foundSomething);
    check("    and what it finds is not one of its bones",
          foundSomething && !ragdollOwnerOf(scene, other.entity, nullptr));
    check("    which is a different entity from the limb", other.entity != hit.entity);
}

// A kerb is one step; a staircase is the same step taken eight times without
// touching the ground in between. The climb ends the tick after it begins -
// rising is what stops a riser blocking - so a character that mounts one tread
// must not bounce off the next.
void testCharacterStaircase() {
    std::printf("Character staircase:\n");

    constexpr float RADIUS = 0.3f, HALF = 0.61f;
    constexpr float RISER = 0.26f, TREAD = 0.85f;
    constexpr int   STEPS = 7;

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {6.0f, 0.5f, 4.0f});
    for (int i = 0; i < STEPS; ++i) {
        const float h = RISER * static_cast<float>(i + 1);
        addBox(scene, {2.0f + static_cast<float>(i) * TREAD, h * 0.5f, 0.0f},
               {TREAD * 0.5f, h * 0.5f, 2.0f});
    }
    // A landing, so what the run measures is the climb and not the fall off
    // the end of it.
    const float top = RISER * static_cast<float>(STEPS);
    addBox(scene, {2.0f + STEPS * TREAD + 1.5f, top * 0.5f, 0.0f},
           {2.0f, top * 0.5f, 2.0f});
    const EntityId walker = addCharacter(scene, RADIUS, HALF, 0.4f);

    float highest = 0.0f;
    for (int i = 0; i < 30; ++i) {
        simulate(scene, 10);
        highest = glm::max(highest, scene.get<Transform>(walker).position.y);
    }

    const glm::vec3 end = scene.get<Transform>(walker).position;
    check("a character climbs a whole staircase",
          end.y > (HALF + RADIUS) + top - RISER);
    check("  arriving at the top of it", end.x > 2.0f + (STEPS - 1) * TREAD);
    // Each tread is mounted, not hopped: a jump from the bottom would clear
    // more than a metre, and one from any tread would overshoot the next.
    check("  a tread at a time, without jumping any of them",
          highest < (HALF + RADIUS) + top + RISER);
}

// A kerb is not a wall, and a character that stops at one is a character that
// cannot use stairs.
void testCharacterStepUp() {
    std::printf("Character step-up:\n");

    constexpr float RADIUS = 0.3f;
    constexpr float HALF   = 0.5f;
    constexpr float KERB   = 0.3f;

    // Floor spanning the walk, and a kerb 2m along it whose top is at 0.3.
    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    addBox(scene, {3.0f, KERB * 0.5f, 0.0f}, {1.0f, KERB * 0.5f, 4.0f});
    const EntityId walker = addCharacter(scene, RADIUS, HALF, 0.4f);

    // Long enough to reach the kerb and walk along its top, short of the far
    // edge - the character steps down off that, and would be back at floor
    // height by the end of a longer run.
    simulate(scene, 120);
    const glm::vec3 stepped = scene.get<Transform>(walker).position;

    check("a character mounts a kerb below its step height",
          stepped.y > (HALF + RADIUS) + KERB * 0.5f);
    check("  standing on top of it, not wedged against it", stepped.x > 3.0f);

    simulate(scene, 120);
    const glm::vec3 descended = scene.get<Transform>(walker).position;
    check("  and steps back down off the far edge",
          descended.x > 4.5f && descended.y < (HALF + RADIUS) + KERB * 0.5f);

    // Mounting is not jumping, and every assertion above passed while it was.
    // The climb asked for stepHeight at walking speed - 5.3 m/s, more than this
    // controller's own jump - and never took it back, so a doorstep launched
    // the character a metre and a half into the air and the trace still ended
    // with them standing on the kerb.
    Scene hop;
    addBox(hop, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    addBox(hop, {3.0f, KERB * 0.5f, 0.0f}, {1.0f, KERB * 0.5f, 4.0f});
    const EntityId mounting = addCharacter(hop, RADIUS, HALF, 0.4f);

    float highest = 0.0f;
    for (int step = 0; step < 24; ++step) {
        simulate(hop, 10);
        highest = glm::max(highest, hop.get<Transform>(mounting).position.y);
    }
    // Standing on the kerb puts the centre at 1.1. Anything much above that was
    // airborne, and a jump would reach nearly 2.5.
    check("  without ever leaving the ground to do it", highest < 1.35f);

    // The same kerb, with step-up switched off. It is a wall again.
    Scene blocked;
    addBox(blocked, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    addBox(blocked, {3.0f, KERB * 0.5f, 0.0f}, {1.0f, KERB * 0.5f, 4.0f});
    const EntityId stopped = addCharacter(blocked, RADIUS, HALF, 0.0f);

    simulate(blocked, 120);
    const glm::vec3 held = blocked.get<Transform>(stopped).position;

    check("stepHeight of zero leaves the kerb a wall", held.x < 2.5f);

    // A step taller than the limit is refused, which is the whole point of the
    // limit: a character that climbs anything is a character that climbs out of
    // the level.
    Scene tall;
    addBox(tall, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 4.0f});
    addBox(tall, {3.0f, 0.6f, 0.0f}, {1.0f, 0.6f, 4.0f});
    const EntityId refused = addCharacter(tall, RADIUS, HALF, 0.4f);

    simulate(tall, 120);
    check("a step above the limit is refused",
          tall.get<Transform>(refused).position.x < 2.5f);
}

// The lab's character carries a ragdoll: kinematic bone bodies riding the
// animated legs, on a layer the character passes through. The step probes used
// to ignore only the character itself, so the clearance sweep hit its own shin
// and reported every staircase as blocked - while the headless test, which has
// no bones, climbed happily.
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

    // A shin: a kinematic capsule at the leg, exactly where the probes look.
    const EntityId shin = scene.createEntity();
    Transform t;
    t.position = {0.35f, 0.4f, 0.0f};
    scene.add<Transform>(shin, std::move(t));
    Rigidbody rb;
    rb.isKinematic = true;
    rb.layer = 2;
    scene.add<Rigidbody>(shin, std::move(rb));
    ColliderPart part;
    part.shape = ColliderShape::Capsule;
    part.radius = 0.06f;
    part.halfHeight = 0.2f;
    Collider c;
    c.parts = { part };
    scene.add<Collider>(shin, std::move(c));

    // Carried along by hand each tick, as RagdollSystem carries bones.
    for (int i = 0; i < 12; ++i) {
        simulate(scene, 10);
        scene.get<Transform>(shin).position =
            scene.get<Transform>(walker).position + glm::vec3(0.35f, -0.6f, 0.0f);
    }

    const glm::vec3 end = scene.get<Transform>(walker).position;
    check("a character steps up with a bone riding its leg",
          end.y > (HALF + RADIUS) + KERB * 0.5f && end.x > 3.0f);
}

// A world's simulation must be a function of the world, not of the order it was
// assembled in. SparseSet is dense and swap-and-pop, so a storage walk carries
// the history of every insertion and removal - and both Gauss-Seidel loops in a
// tick are order-dependent. Physics canonicalises its two lists on entity slot
// and says so where it does it; this is the test that says so too, because a
// comment cannot fail.
void testTheSameWorldSimulatesTheSameWay() {
    std::printf("A world assembled two different ways:\n");

    constexpr int STACKED = 12;

    // The same bodies under the same names in both worlds: entities are created
    // in the same order, so a slot names the same box in each. What differs is
    // the order components were added, which is what a dense walk follows.
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

        // Created first and all together, so both worlds agree on which slot is
        // which box however the components arrive afterwards.
        std::vector<EntityId> boxes;
        boxes.reserve(STACKED);
        for (int i = 0; i < STACKED; ++i) boxes.push_back(scene.createEntity());

        // One column, every box on the one below, every aabbMin.x identical to
        // the bit: boxes that miss make no manifold to order, and boxes at
        // different x are split by the sweep before the tie-break can run.
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

    check("both worlds hold the same bodies",
          forwards.entityCount() == backwards.entityCount());

    // While they are still falling into each other. Compared after everything
    // has come to rest, two solve orders would agree anyway and this would pass
    // whatever the pipeline did.
    simulate(forwards, 45);
    simulate(backwards, 45);

    size_t compared = 0;
    size_t differed = 0;
    size_t moving   = 0;
    float  worst    = 0.0f;
    forwards.forEachEntity([&](EntityId id) {
        if (!forwards.has<Rigidbody>(id) || !forwards.has<Transform>(id)) return;
        if (forwards.get<Rigidbody>(id).isStatic) return;
        if (!backwards.isAlive(id) || !backwards.has<Transform>(id)) return;

        ++compared;
        if (glm::length(forwards.get<Rigidbody>(id).linearVelocity) > 0.01f) ++moving;

        // Compared exactly. Two runs of the same arithmetic on the same machine
        // either agree to the bit or the ordering is not canonical, and "close
        // enough" would pass a world that had quietly diverged.
        const glm::vec3 a = forwards.get<Transform>(id).position;
        const glm::vec3 b = backwards.get<Transform>(id).position;
        const float apart = glm::length(a - b);
        if (apart > worst) worst = apart;

        // Velocity as well as pose: two worlds can pass through the same place
        // on their way somewhere different.
        const glm::vec3 va = forwards.get<Rigidbody>(id).linearVelocity;
        const glm::vec3 vb = backwards.get<Rigidbody>(id).linearVelocity;
        if (a != b || va != vb) ++differed;
    });

    check("  and every box was compared", compared == STACKED);
    check("  while they were still moving", moving > 0);
    check("a tick is a function of the world, not of how it was built",
          differed == 0);
    if (differed != 0) {
        std::printf("      %zu of %zu differ, worst %.6f m\n",
                    differed, compared, static_cast<double>(worst));
    }
}

} // namespace

void runPhysicsTests() {
    testRaycastShapes();
    testRaycastMisses();
    testRaycastFilters();
    testSpherecast();
    testCapsuleBoxFindsSmallOverlaps();
    testGjk();
    testQueryAgainstNewShapes();
    testCollisionLayers();
    testRestOnNarrowSupport();
    testMeshCollider();
    testMeshDoesNotEjectDownward();
    testOffsetMeshPartCollides();
    testRebuildMeshBvhWithoutMesh();
    testJoints();
    testJointStiffnessIsIterationIndependent();
    testJointWakesASleeper();
    testDistanceJointPinnedToWorld();
    testPrefabKeepsItsJoints();
    testRagdoll();
    testRagdollFallsTwice();
    testRagdollPose();
    testRagdollAsHitboxes();
    testCharacterStaircase();
    testCharacterStepUp();
    testStepUpPastOwnBones();
    testTheSameWorldSimulatesTheSameWay();
}
