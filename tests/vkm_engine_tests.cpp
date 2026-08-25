// The vkm_core test suite. Deliberately covers only what runs without a GL
// context or a window: engine logic that is pure computation over a Scene.
// Anything needing a live context or a frame belongs in a rendering test, not
// here - this binary must stay runnable on a build machine with no GPU.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/clock.h"
#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "core/event/event_bus.h"
#include "core/system.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/name.h"
#include "ecs/component/physics/ragdoll.h"
#include "io/scene/component_serializer.h"
#include "io/scene/prefab.h"
#include "system/animation/pose_buffer.h"
#include "system/animation/ragdoll_pose.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "system/hierarchy/hierarchy_system.h"
#include "resource/asset/mesh_asset.h"
#include "system/physics/authoring/mesh_collider.h"
#include "system/physics/authoring/ragdoll_build.h"
#include "system/physics/ragdoll_system.h"
#include "system/physics/collision/mesh_bvh.h"
#include "ecs/component/physics/rigidbody.h"
#include "platform/input/input_map.h"
#include "platform/window/window_manager.h"
#include "resource/resource_manager.h"
#include "system/physics/character/character_controller_system.h"
#include "system/physics/physics_system.h"
#include "system/physics/collision/gjk.h"
#include "system/physics/collision/narrowphase.h"
#include "system/physics/collision/support.h"
#include "system/physics/query/query.h"

namespace {

using namespace Vkm::Engine;

int g_failures = 0;

void check(const char* what, bool ok) {
    if (!ok) ++g_failures;
    std::printf("  %-62s %s\n", what, ok ? "ok" : "<-- FAILED");
}

// Distances and normals come out of divisions and square roots, so they are
// compared to a tolerance rather than for equality. It is far looser than the
// error being allowed for, because what these assert is which surface was hit,
// not how precisely it was located.
bool near(float a, float b) {
    return std::fabs(a - b) < 1e-3f;
}

// A static body with one collider part, which is all a query looks at.
EntityId addBody(
    Scene& scene,
    const glm::vec3& position,
    ColliderShape shape,
    const glm::quat& rotation = {1.0f, 0.0f, 0.0f, 0.0f}
) {
    const EntityId id = scene.createEntity();

    Transform transform;
    transform.position = position;
    transform.rotation = rotation;
    scene.add<Transform>(id, std::move(transform));

    Rigidbody body;
    body.isStatic = true;
    scene.add<Rigidbody>(id, std::move(body));

    ColliderPart part;
    part.shape = shape;
    Collider collider;
    collider.parts = { part };
    scene.add<Collider>(id, std::move(collider));

    return id;
}

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
    check("  at its near face, not its centre", near(hit.distance, 4.5f));
    check("  with the normal facing the ray", near(hit.normal.x, -1.0f));
    check("  and the point on that face", near(hit.point.x, 4.5f));

    check("a capsule behind is hit when cast at",
          raycast(scene, {0,0,0}, {-1,0,0}, 100.0f, hit));
    check("  the capsule entity is reported", hit.entity == capsule);
    check("  at radius off its axis", near(hit.distance, 4.5f));

    // The capsule's segment is +-0.5 on Y, swept by 0.5, so it spans [-1, 1].
    check("its cap is hit from above",
          raycast(scene, {-5.0f, 5.0f, 0.0f}, {0,-1,0}, 100.0f, hit));
    check("  at the top of the sweep", near(hit.distance, 4.0f));
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
          near(hit.distance, 5.0f - std::sqrt(0.5f)));
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
    check("  and normalizes to the same answer", near(hit.distance, 4.5f));

    check("a ray starting inside a body reports it",
          raycast(scene, {5.0f, 0.0f, 0.0f}, {1,0,0}, 100.0f, hit));
    check("  at distance zero", near(hit.distance, 0.0f));
    check("  with a normal facing the caster", near(hit.normal.x, -1.0f));
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
    check("  at its distance", near(hit.distance, 1.5f));

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
              && near(hit.distance, 4.25f));
    check("  and reports the surface, not its own centre",
          near(hit.point.x, 4.5f));
    check("  the box entity is reported", hit.entity == box);

    // The ray passes over the box; the sphere is wide enough to catch its edge.
    // This is the whole reason the query exists.
    check("a ray at 0.6 clears a box of half extent 0.5",
          !raycast(scene, {0.0f, 0.6f, 0.0f}, {1,0,0}, 100.0f, hit));
    check("a sphere of 0.25 at the same height does not",
          spherecast(scene, {0.0f, 0.6f, 0.0f}, 0.25f, {1,0,0}, 100.0f, hit));
    check("  stopping on the edge it clips",
          near(hit.distance, 4.27087f));

    // Aimed at a corner, where a slab test alone would stop the sphere on a
    // face plane it never reaches.
    Scene corner;
    addBody(corner, {0.0f, 0.0f, 0.0f}, ColliderShape::Box);
    const float diagonal = std::sqrt(3.0f * 4.5f * 4.5f);
    check("a sweep into a corner stops on the corner",
          spherecast(corner, {5,5,5}, 0.25f, {-1,-1,-1}, 100.0f, hit)
              && near(hit.distance, diagonal - 0.25f));

    Scene capsule;
    addBody(capsule, {-5.0f, 0.0f, 0.0f}, ColliderShape::Capsule);
    check("a swept sphere stops a combined radius from a capsule axis",
          spherecast(capsule, {0,0,0}, 0.25f, {-1,0,0}, 100.0f, hit)
              && near(hit.distance, 4.25f));

    check("a sweep starting overlapped stops where it started",
          spherecast(scene, {5.0f, 0.0f, 0.0f}, 0.25f, {1,0,0}, 100.0f, hit)
              && near(hit.distance, 0.0f));
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
              && near(hit.distance, asRay.distance));
}

// A static box of a given size, which is what a floor and a kerb both are.
EntityId addBox(Scene& scene, const glm::vec3& center,
                const glm::vec3& halfExtents) {
    const EntityId id = scene.createEntity();

    Transform transform;
    transform.position = center;
    scene.add<Transform>(id, std::move(transform));

    Rigidbody body;
    body.isStatic = true;
    scene.add<Rigidbody>(id, std::move(body));

    ColliderPart part;
    part.shape = ColliderShape::Box;
    part.halfExtents = halfExtents;
    Collider collider;
    collider.parts = { part };
    scene.add<Collider>(id, std::move(collider));

    return id;
}

// A capsule character standing with its feet on y = 0, walking along +X.
EntityId addCharacter(Scene& scene, float radius, float halfHeight,
                      float stepHeight) {
    const EntityId id = scene.createEntity();

    Transform transform;
    transform.position = {0.0f, halfHeight + radius, 0.0f};
    scene.add<Transform>(id, std::move(transform));

    Rigidbody body;
    body.freezeRotation = true;
    body.canSleep = false;
    body.mass = 70.0f;
    scene.add<Rigidbody>(id, std::move(body));

    ColliderPart part;
    part.shape = ColliderShape::Capsule;
    part.radius = radius;
    part.halfHeight = halfHeight;
    Collider collider;
    collider.parts = { part };
    scene.add<Collider>(id, std::move(collider));

    CharacterController controller;
    controller.moveInput = {2.0f, 0.0f, 0.0f};
    controller.stepHeight = stepHeight;
    scene.add<CharacterController>(id, std::move(controller));

    return id;
}

// Drive the two systems the way the Simulation stage does - physics first, then
// the controller reading this tick's contacts - for long enough to walk into
// something two metres away.
void simulate(Scene& scene, int ticks) {
    ResourceManager resources;
    Clock clock;
    EventBus events;
    WindowManager window;   // never given a window: nothing here draws
    InputMap input;
    FrameContext ctx{scene, resources, clock, events, window, input};

    PhysicsSystem physics;
    CharacterControllerSystem controller;
    HierarchySystem hierarchy;

    for (int tick = 0; tick < ticks; ++tick) {
        physics.fixedUpdate(ctx);
        controller.fixedUpdate(ctx);
        // The Transform stage, which the app runs every frame and which a
        // parented body's pose is resolved through. Without it a ragdoll's
        // bones - children of the character since they are its rig - would be
        // read in the wrong frame here and only here.
        hierarchy.update(ctx);
    }
}

// The reason spherecast was built. A kerb is not a wall, and a character that
// stops at one is a character that cannot use stairs.
// A kerb is one step; a staircase is the same step taken eight times without
// touching the ground in between, and only the first of those was ever tested.
// The climb used to end the tick after it began - rising is what stops a riser
// blocking - so the character mounted one tread and then bounced against the
// next for as long as anyone held the key.
// A body resting on a support narrower than itself. The SAT used to hand this
// to the edge-edge case - two horizontal edges cross to a vertical axis, the
// face normal's twin, and under a whisker of tilt the duplicate won by float
// noise - so a four-point face manifold became one corner, and the position
// correction rocked the body on that corner for ever at constant speed.
// Every collider in a loaded scene is offered to rebuildMeshBvh, and almost
// none of them have a mesh part. A bounds check that read the part before
// proving it existed dereferenced a null pointer on the first box in the lab -
// which the suite missed entirely, because no test had ever handed this a
// collider that was not already a mesh.
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
          std::fabs(scene.get<Joint>(weight).distance - 2.5f) < 0.05f);
}

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

bool sameDirection(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(a - b) < 1e-3f;
}

// The convention this engine turns on, pinned so that changing it is a decision
// rather than an accident. Right-handed, +Y up, forward -Z - glm's own, and the
// view space the renderer works in, which is what makes screen-right the plain
// +X and the obvious line of code the correct one.
//
// It was +Z forward once, which put right at -X and shipped three
// mirrored controls. These three assertions are what made changing it a
// deliberate act: they failed, and were changed on purpose. The rest below hold
// under either convention and are what would have caught a half-finished flip.
void testMathConvention() {
    std::printf("Math - the axis convention:\n");

    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 forward = Math::computeForward(identity);
    const glm::vec3 right   = Math::computeRight(identity);
    const glm::vec3 up      = Math::computeUp(identity);

    check("forward is -Z", sameDirection(forward, {0.0f, 0.0f, -1.0f}));
    check("up is +Y",      sameDirection(up,      {0.0f, 1.0f,  0.0f}));
    check("right is +X",   sameDirection(right,   {1.0f, 0.0f,  0.0f}));

    // Convention-independent: whatever forward means, the three have to agree.
    // A half-finished change of convention shows up here rather than as a
    // character walking sideways.
    check("the basis is right-handed: right x up == -forward",
          sameDirection(glm::cross(right, up), -forward));
    check("  and orthogonal", std::fabs(glm::dot(right, up)) < 1e-3f
                           && std::fabs(glm::dot(right, forward)) < 1e-3f
                           && std::fabs(glm::dot(up, forward)) < 1e-3f);

    // Also convention-independent, and the one that would have caught the bug
    // it was written for: glm::quatLookAt builds for -Z, so calling it directly
    // aims everything exactly backwards.
    const glm::vec3 targets[] = {
        { 1.0f,  0.0f,  0.0f}, {-1.0f,  0.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f}, { 0.0f,  0.0f, -1.0f},
        { 1.0f,  0.0f,  1.0f}, {-0.4f,  0.3f,  0.9f}
    };
    bool roundTrips = true;
    for (const glm::vec3& target : targets) {
        const glm::vec3 aimed = Math::computeForward(Math::lookRotation(target));
        if (!sameDirection(aimed, glm::normalize(target))) roundTrips = false;
    }
    check("lookRotation faces what it is given", roundTrips);

    // A quarter turn about up takes forward onto right, in that direction and
    // no other. Gets the sign of a rotation wrong and this is what says so.
    const glm::quat quarter = glm::angleAxis(glm::half_pi<float>(), up);
    check("a quarter turn about up sends forward to -right",
          sameDirection(Math::computeForward(quarter), -right));

    // The pair the flip broke, and broke silently: a look control maps
    // angles to a rotation and reverses it to re-derive them, and the flip
    // moved one side. The camera flew correctly and jumped the moment anything
    // took hold of one it had not moved - which passes a quick try, because
    // the first thing anyone does still works.
    bool anglesRoundTrip = true;
    bool pitchRaises = true;
    for (float yawDeg : {0.0f, 37.0f, -120.0f, 179.0f}) {
        for (float pitchDeg : {0.0f, 42.0f, -60.0f}) {
            const float yaw = glm::radians(yawDeg);
            const float pitch = glm::radians(pitchDeg);

            const glm::quat aim = Math::fromYawPitch(yaw, pitch);
            const glm::vec3 aimed = Math::computeForward(aim);
            float backYaw = 0.0f;
            float backPitch = 0.0f;
            Math::toYawPitch(aimed, backYaw, backPitch);

            const glm::vec3 again =
                Math::computeForward(Math::fromYawPitch(backYaw, backPitch));
            if (!sameDirection(aimed, again)) anglesRoundTrip = false;
            // A rising pitch has to raise the view, whatever the convention.
            if (pitchDeg > 0.0f && aimed.y <= 0.0f) pitchRaises = false;
            if (pitchDeg < 0.0f && aimed.y >= 0.0f) pitchRaises = false;
        }
    }
    check("angles round-trip through a direction and back", anglesRoundTrip);
    check("  and a rising pitch raises the view", pitchRaises);

    // Yaw zero has to face forward, or every authored angle means something
    // else than it did.
    float zeroYaw = 0.0f;
    float zeroPitch = 0.0f;
    Math::toYawPitch(forward, zeroYaw, zeroPitch);
    check("  with yaw and pitch zero looking straight ahead",
          std::fabs(zeroYaw) < 1e-3f && std::fabs(zeroPitch) < 1e-3f);

    check("worldRotationOf recovers a rotation from its matrix",
          sameDirection(Math::computeForward(
              Math::worldRotationOf(glm::mat4_cast(quarter))),
              Math::computeForward(quarter)));
}

BoxShape boxAt(const glm::vec3& center, const glm::vec3& halfExtents) {
    BoxShape box;
    box.center = center;
    box.halfExtents = halfExtents;
    return box;
}

// GJK has to agree with the routine the engine already trusts. Box against box
// is the one pair both can answer, so it is the one that can be checked against
// something other than my own arithmetic.
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
    check("  at the depth they share", near(gjk.penetration, 0.2f));

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


// A dynamic body with nothing holding it up, so what a joint does is the only
// thing under test.
EntityId addFallingBody(Scene& scene, const glm::vec3& position, float half) {
    const EntityId id = scene.createEntity();

    Transform transform;
    transform.position = position;
    scene.add<Transform>(id, std::move(transform));

    Rigidbody body;
    body.mass = 5.0f;
    body.canSleep = false;
    scene.add<Rigidbody>(id, std::move(body));

    ColliderPart part;
    part.shape = ColliderShape::Box;
    part.halfExtents = glm::vec3(half);
    Collider collider;
    collider.parts = { part };
    scene.add<Collider>(id, std::move(collider));

    return id;
}

// An anchor: a static body with no collider, which is a thing to hang from
// rather than a thing to hit.
EntityId addAnchor(Scene& scene, const glm::vec3& position) {
    const EntityId id = scene.createEntity();
    Transform transform;
    transform.position = position;
    scene.add<Transform>(id, std::move(transform));
    Rigidbody body;
    body.isStatic = true;
    scene.add<Rigidbody>(id, std::move(body));
    return id;
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

// A rig shaped like something with limbs: a spine with two legs, which is the
// smallest skeleton where "did the joints hold it together" means anything.
SkeletonAsset makeTestRig() {
    SkeletonAsset rig;
    const char* names[] = {"hips", "spine", "chest",
                           "legL", "footL", "legR", "footR"};
    const int32_t parents[] = {-1, 0, 1, 0, 3, 0, 5};
    const glm::vec3 offsets[] = {
        {0.0f, 1.0f, 0.0f},    // hips, a metre up
        {0.0f, 0.3f, 0.0f},    // spine
        {0.0f, 0.3f, 0.0f},    // chest
        {-0.15f, -0.45f, 0.0f}, // left leg
        {0.0f, -0.45f, 0.0f},   // left foot
        {0.15f, -0.45f, 0.0f},  // right leg
        {0.0f, -0.45f, 0.0f}    // right foot
    };

    for (int i = 0; i < 7; ++i) {
        rig.bones.push_back({names[i], parents[i]});
        Transform bind;
        bind.position = offsets[i];
        rig.bindPose.push_back(bind);
        rig.inverseBind.push_back(glm::mat4(1.0f));
    }
    return rig;
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
    FrameContext ownedCtx{owned, ownedRes, ownedClock, ownedEvents,
                          ownedWindow, ownedInput};
    lifetime.init(ownedCtx);

    const size_t withRagdoll = owned.entityCount();
    check("a built ragdoll adds its bones to the scene", withRagdoll == 6);
    owned.destroyEntity(doomed);
    check("destroying the character takes its bones with it",
          owned.entityCount() == 0);
    lifetime.shutdown();
}

// A flat grid of triangles, which is what a floor is once it stops being a box.
MeshAsset makeGridMesh(int cells, float size) {
    MeshAsset mesh;
    const float step = size / static_cast<float>(cells);
    const float half = size * 0.5f;

    for (int z = 0; z <= cells; ++z) {
        for (int x = 0; x <= cells; ++x) {
            Vertex vertex;
            vertex.position = {-half + x * step, 0.0f, -half + z * step};
            vertex.normal = {0.0f, 1.0f, 0.0f};
            mesh.vertices.push_back(vertex);
        }
    }
    const uint32_t stride = static_cast<uint32_t>(cells + 1);
    for (int z = 0; z < cells; ++z) {
        for (int x = 0; x < cells; ++x) {
            const uint32_t a = static_cast<uint32_t>(z) * stride + x;
            mesh.indices.insert(mesh.indices.end(),
                                {a, a + stride, a + 1,
                                 a + 1, a + stride, a + stride + 1});
        }
    }
    return mesh;
}

EntityId addMeshBody(Scene& scene, const MeshAsset& mesh) {
    const EntityId id = scene.createEntity();
    scene.add<Transform>(id, Transform{});

    Rigidbody body;
    body.isStatic = true;
    scene.add<Rigidbody>(id, std::move(body));

    Collider collider;
    collider.parts.clear();   // the default unit box would be the thing hit
    addMeshCollider(collider, mesh);
    scene.add<Collider>(id, std::move(collider));

    return id;
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

// The shape a model import actually leaves behind: physics on the root, the rig
// on a node beneath it. Everything authored on the root has to reach downward,
// and everything driven from the rig node has to reach up.
void testImportedHierarchy() {
    std::printf("Authoring across an imported hierarchy:\n");

    Scene scene;
    const EntityId root = scene.createEntity();
    scene.add<Transform>(root, Transform{});
    scene.add<Name>(root, makeName("Character"));

    const EntityId rigNode = scene.createEntity();
    scene.add<Transform>(rigNode, Transform{});
    scene.add<Name>(rigNode, makeName("RootNode"));
    scene.add<Animator>(rigNode, Animator{});
    HierarchyOperations::setParent(scene, rigNode, root);

    const EntityId meshNode = scene.createEntity();
    scene.add<Transform>(meshNode, Transform{});
    scene.add<Mesh>(meshNode, Mesh{});
    HierarchyOperations::setParent(scene, meshNode, rigNode);

    check("the rig is found from the root",
          HierarchyOperations::findInSelfOrDescendants<Animator>(scene, root)
              == rigNode);
    check("the mesh is found from the root, two deep",
          HierarchyOperations::findInSelfOrDescendants<Mesh>(scene, root)
              == meshNode);
    check("an entity carrying it is its own answer",
          HierarchyOperations::findInSelfOrDescendants<Animator>(scene, rigNode)
              == rigNode);

    // The other direction: a ragdoll authored on the root, reached from the rig
    // node the animation system walks.
    scene.add<Ragdoll>(root, Ragdoll{});
    check("a ragdoll on the root is found from the rig node",
          HierarchyOperations::findInSelfOrAncestors<Ragdoll>(scene, rigNode)
              == root);
    check("  and from deeper still",
          HierarchyOperations::findInSelfOrAncestors<Ragdoll>(scene, meshNode)
              == root);
    check("something absent is absent in both directions",
          !HierarchyOperations::findInSelfOrAncestors<Collider>(scene, meshNode)
       && !HierarchyOperations::findInSelfOrDescendants<Collider>(scene, root));

    // And the build places bones in the rig node's frame rather than the
    // selected entity's, which matters the moment the two differ.
    Scene offset;
    const EntityId body = offset.createEntity();
    Transform bodyAt;
    bodyAt.position = {10.0f, 0.0f, 0.0f};
    offset.add<Transform>(body, std::move(bodyAt));

    const EntityId rigAt = offset.createEntity();
    Transform rigOffset;
    rigOffset.position = {0.0f, 2.0f, 0.0f};   // a metre or two above the root
    offset.add<Transform>(rigAt, std::move(rigOffset));
    offset.add<Animator>(rigAt, Animator{});
    HierarchyOperations::setParent(offset, rigAt, body);

    const SkeletonAsset skeleton = makeTestRig();
    const uint32_t built = buildRagdoll(offset, body, skeleton);
    check("a ragdoll builds from the root", built == 4);
    check("  with the component where it was asked for",
          offset.has<Ragdoll>(body));

    // The hips bone sits a metre up the rig; in the rig node's frame that is
    // 10 across and 3 up, and in the root's it would be 10 across and 1. Asked
    // in world, because a bone is parented to the character it belongs to and
    // so its own Transform is measured from there.
    const EntityId hips = offset.get<Ragdoll>(body).bones[0].body;
    const glm::vec3 at =
        glm::vec3(HierarchyOperations::computeWorldMatrix(offset, hips)[3]);
    check("  and its bones in the rig node's frame, not the root's",
          at.x > 9.0f && at.y > 2.5f);
    // Grouped under one node rather than tipped into the character's child
    // list, and that node under the character - so the rig travels with it.
    const EntityId group = offset.get<Ragdoll>(body).root;
    check("  grouped under a node of their own",
          group && offset.has<Hierarchy>(hips)
       && offset.get<Hierarchy>(hips).parent == group);
    check("  which is itself under the character",
          offset.has<Hierarchy>(group)
       && offset.get<Hierarchy>(group).parent == body);
    check("  and the bones are siblings, not nested inside each other",
          offset.get<Hierarchy>(offset.get<Ragdoll>(body).bones[1].body).parent
              == group);
}

// What survives being written down. A component whose fields round-trip is not
// the same as one that still works afterwards, and both ragdolls and joints
// carry a reference to another entity, which is the part that goes wrong.
void testComponentRoundTrip() {
    std::printf("Round-tripping the new components:\n");

    Ragdoll before;
    before.active = true;
    const glm::mat4 offset =
        glm::translate(glm::mat4(1.0f), {0.0f, 1.5f, 0.0f});
    before.bones.push_back({3, EntityId{7, 2}, offset});
    before.bones.push_back({5, EntityId{9, 1}, glm::mat4(1.0f)});

    Ragdoll after;
    ComponentSerializer::load(ComponentSerializer::save(before), after);

    check("a ragdoll keeps its switch", after.active);
    // The bodies are entities the scene saves anyway. Losing the mapping does
    // not save a ragdoll without its bodies - it saves one that has forgotten
    // them, beside a loose skeleton that falls.
    check("  and the bones that map bodies to it", after.bones.size() == 2);
    if (after.bones.size() == 2) {
        check("    naming the same bones", after.bones[0].bone == 3
                                        && after.bones[1].bone == 5);
        check("    and the same body slots", after.bones[0].body.index == 7
                                          && after.bones[1].body.index == 9);
        // Without this the body sits where the bone is, and every limb is
        // offset by half its own length the moment physics takes over.
        check("    keeping the offset from body to bone",
              near(after.bones[0].boneFromBody[3][1], 1.5f));
    }

    Joint joint;
    joint.type = JointType::Distance;
    joint.connected = EntityId{4, 3};
    joint.anchor = {0.0f, 0.5f, 0.0f};
    joint.distance = 2.5f;
    joint.stiffness = 0.4f;
    joint.collideConnected = true;

    Joint loaded;
    ComponentSerializer::load(ComponentSerializer::save(joint), loaded);

    check("a joint keeps its type", loaded.type == JointType::Distance);
    check("  its connected slot", loaded.connected.index == 4);
    check("  its anchor", near(loaded.anchor.y, 0.5f));
    check("  its length and give", near(loaded.distance, 2.5f)
                                && near(loaded.stiffness, 0.4f));
    check("  and whether the pair still collides", loaded.collideConnected);

    // A collider's points and its parts' spans into them, which is the whole
    // description of a mesh: lose either and the shape is empty.
    Collider collider;
    collider.parts.clear();
    collider.meshPoints = {{0,0,0}, {1,0,0}, {0,1,0}};
    ColliderPart part;
    part.shape = ColliderShape::Mesh;
    part.meshCount = 3;
    collider.parts = { part };

    Collider back;
    ComponentSerializer::load(ComponentSerializer::save(collider), back);
    check("a mesh collider keeps its points", back.meshPoints.size() == 3);
    const bool span = back.parts.size() == 1
                   && back.parts[0].meshCount == 3
                   && back.parts[0].shape == ColliderShape::Mesh;
    check("  and the span that reads them", span);
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
    check("  at the surface", near(hit.distance, 4.0f));
    check("  facing up out of it", hit.normal.y > 0.9f);

    check("a sweep finds it too, a radius short",
          spherecast(terrain, {3.0f, 4.0f, -2.0f}, 0.5f, {0,-1,0}, 100.0f, hit)
              && near(hit.distance, 3.5f));

    check("a ray past the edge of the mesh finds nothing",
          !raycast(terrain, {20.0f, 4.0f, 0.0f}, {0,-1,0}, 100.0f, hit));
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
          near(global[slice + 0][3][1], 1.0f));
    check("  and a bone with no body of its own follows its parent",
          near(global[slice + 2][3][1], 1.6f));   // chest: hips + spine + chest

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
          near(global[slice + 0][3][0], SHIFT));
    check("  carrying the bones that have no body along",
          near(global[slice + 2][3][0], SHIFT));

    // The rig's own transform is the frame the pose is in, so posing through a
    // moved rig has to take it back out - or a character walks away from its
    // own skeleton at twice the speed.
    const glm::mat4 rigWorld =
        glm::translate(glm::mat4(1.0f), {SHIFT, 0.0f, 0.0f});
    composeRagdollPose(ragdoll, gatherRagdollBodies(scene, ragdoll),
                       skeleton, rigWorld, poses.writeTo(slice));
    check("the pose is relative to the rig, not the world",
          near(global[slice + 0][3][0], 0.0f));

    // The palette is what the vertex shader reads, and a pose written without
    // one draws a character in its bind shape however the bones moved.
    const std::vector<glm::mat4>& palette = poses.palette();
    check("the skinning palette is written beside the pose",
          palette.size() == global.size()
       && near(palette[slice + 0][3][1], global[slice + 0][3][1]));

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
    check("a bone whose body is gone falls back to its parent",
          std::isfinite(second.global()[slice2 + 1][3][1]));
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
}

// A ragdoll doubles as a hit box rig: its bones are bodies with colliders, so a
// query already picks one out. This is the whole path a game walks - shoot,
// find the limb, find whose it is.
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
    FrameContext ctx{scene, resources, clock, events, window, input};
    ragdolls.update(ctx);

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

    QueryFilter withoutBones;
    withoutBones.layerMask = ~RagdollSettings{}.boneLayer;
    check("  or for everything except them",
          !raycast(scene, {5.0f, 1.3f, 0.0f}, {-1,0,0}, 20.0f, hit, withoutBones)
       || ragdollOwnerOf(scene, hit.entity, nullptr) != character);
}

} // namespace

int main() {
    // Engine code asserts and logs through vkmLog, so the logger has to exist
    // before a Scene does. ERROR level keeps expected noise out of the output,
    // and the temp directory keeps the file out of wherever ctest was run from.
    const std::filesystem::path logPath =
        std::filesystem::temp_directory_path() / "vkm_engine_tests.log";
    Vkm::Log::Logger::init(logPath.string(), "VKM_ENGINE-TESTS",
                           Vkm::Log::LogLevel::ERROR);

    testMathConvention();
    testGjk();
    testRaycastShapes();
    testRaycastMisses();
    testRaycastFilters();
    testSpherecast();
    testCharacterStepUp();
    testCharacterStaircase();
    testDistanceJointPinnedToWorld();
    testRebuildMeshBvhWithoutMesh();
    testRestOnNarrowSupport();
    testPrefabKeepsItsJoints();
    testStepUpPastOwnBones();
    testJoints();
    testRagdoll();
    testMeshCollider();
    testImportedHierarchy();
    testComponentRoundTrip();
    testQueryAgainstNewShapes();
    testRagdollPose();
    testCollisionLayers();
    testRagdollAsHitboxes();

    std::printf(g_failures ? "\n%d FAILURE(S)\n" : "\nALL OK\n", g_failures);
    return g_failures ? 1 : 0;
}
