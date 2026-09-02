// The vkm_core test suite. Deliberately covers only what runs without a GL
// context or a window: engine logic that is pure computation over a Scene.
// Anything needing a live context or a frame belongs in a rendering test, not
// here - this binary must stay runnable on a build machine with no GPU.

#include <cmath>
#include <limits>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/clock.h"
#include "net/wire/bit_stream.h"
#include "net/wire/quantize.h"
#include "net/wire/codecs.h"
#include "net/prediction/command.h"
#include "net/transport/connection.h"
#include "net/transport/reliable.h"
#include "net/prediction/rewind.h"
#include "net/wire/schema.h"
#include "net/prediction/interpolation.h"
#include "net/net_session.h"
#include "net/replication/snapshot.h"
#include "net/replication/silence.h"
#include "net/replication/spawn.h"
#include "platform/net/udp_socket.h"

// UdpSocket::send refuses a zero-length datagram on purpose - this end never
// emits one. A peer elsewhere is under no such rule, so exercising what happens
// when one arrives means sending it the way the network can.
#if defined(_WIN32)
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif
#include "core/math/random.h"
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
#include "io/project.h"
#include "io/scene/component_serializer.h"
#include "io/scene/prefab.h"
#include "io/scene/scene_serializer.h"
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
#include "system/script/script_component.h"

namespace {

using namespace Vkm::Engine;

int g_failures = 0;

/**
 * @brief The angle between two rotations, in degrees.
 *
 * Through the absolute dot product, because q and -q are the same rotation and
 * the wire encoding drops the sign. Comparing components directly, or taking
 * the angle of inverse(a)*b without the absolute value, reports 360 degrees for
 * two rotations that are identical.
 */
float rotationErrorDegrees(const glm::quat& a, const glm::quat& b) {
    const float dot = std::abs(glm::dot(glm::normalize(a), glm::normalize(b)));
    return glm::degrees(2.0f * std::acos(std::min(1.0f, dot)));
}

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
    NetSession net;
    FrameContext ctx{scene, resources, clock, events, window, input, net};

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

// The unit a tick consumes. Input arrives on the frame clock and simulation
// runs on the tick clock, so a fixed update reading frame state directly would
// drop a tap taken between two ticks and repeat a press across every tick of a
// slow frame; the command is built per tick to close both.
// The latch surviving a frame with no tick, and draining so a second tick does
// not re-see a press, are not covered: both need key events, and key state
// reaches the engine only through GLFW callbacks.
// A command has to say everything a tick did with it. Axes alone do not: a
// character steers relative to a view, the view turns on the render clock, and
// a tick that asks the scene for it reads whatever the last frame left there.
void testCommandCarriesTheView() {
    std::printf("A command carries the view it was aimed with:\n");

    InputMap map;
    const glm::quat aimed  = glm::angleAxis(glm::radians(90.0f),  Math::WORLD_UP);
    const glm::quat turned = glm::angleAxis(glm::radians(-30.0f), Math::WORLD_UP);

    map.setView(aimed);
    map.beginTick(1);
    check("the tick's command takes the view it was built with",
          glm::all(glm::epsilonEqual(map.command().view, aimed, 1e-6f)));

    // The frame turns the camera again, which is what a frame does. The tick
    // already running must not follow it, or replaying its command walks
    // somewhere the original did not.
    map.setView(turned);
    check("  and does not follow the view once it is built",
          glm::all(glm::epsilonEqual(map.command().view, aimed, 1e-6f)));

    map.beginTick(2);
    check("  while the next tick takes the view as it now stands",
          glm::all(glm::epsilonEqual(map.command().view, turned, 1e-6f)));
}

void testInputCommand() {
    std::printf("Per-tick input command:\n");

    InputMap map;
    check("an undefined action has no command slot", map.indexOf("Nothing") < 0);

    map.define("Move/Forward", { InputBinding{InputSource::Key, 87, 1.0f} });
    map.define("Jump",         { InputBinding{InputSource::Key, 32, 1.0f} });
    check("defining an action gives it a slot", map.indexOf("Move/Forward") == 0);
    check("  and the next one the next slot", map.indexOf("Jump") == 1);

    // Stable for the session: a replayed command has to mean what it meant when
    // it was recorded, which it cannot if a slot moved under it.
    map.addBinding("Move/Forward", InputBinding{InputSource::Key, 265, 1.0f});
    map.define("Move/Forward", { InputBinding{InputSource::Key, 87, 1.0f} });
    check("  and redefining an action keeps it", map.indexOf("Move/Forward") == 0);

    // Before the first tick a reader gets "nothing held" rather than whatever
    // the last session left, so a system that runs early is not fed stale input.
    check("the pre-tick command is zeroed",
          map.command().sequence == 0 && map.command().pressed == 0
       && near(map.command().axis[0], 0.0f));

    map.beginTick(7);
    const InputCommand first = map.command();
    check("a built command carries the tick it drives", first.tick == 7);
    check("  and a sequence number of its own", first.sequence == 1);

    map.beginTick(8);
    check("the next tick is a new command", map.command().tick == 8
       && map.command().sequence == 2);

    // Sequence and tick are separate because a replay re-runs old ticks: the
    // tick repeats, the sequence that acknowledged it does not.
    check("  so sequence and tick are not the same number",
          map.command().sequence != map.command().tick);

    // An action past the cap is still readable at frame rate; it just has no
    // room in a command, which is a warning rather than a failure.
    for (int i = 0; i < static_cast<int>(MAX_INPUT_ACTIONS) + 4; ++i) {
        map.define("Filler" + std::to_string(i), { InputBinding{InputSource::Key, 100 + i, 1.0f} });
    }
    check("an action past the command cap has no slot",
          map.indexOf("Filler" + std::to_string(MAX_INPUT_ACTIONS + 3)) < 0);
    check("  while the ones that fit kept theirs", map.indexOf("Move/Forward") == 0);
}

// The cadence a project asks for is the cadence fixedUpdate runs at, and for a
// networked game the rate the wire is clocked by.
void testTickRate() {
    std::printf("Tick rate:\n");

    Clock clock;
    check("a fresh clock ticks at the engine default",
          near(clock.getFixedStep(), 1.0f / static_cast<float>(Config::DEFAULT_TICK_RATE)));

    clock.setTickRate(128);
    check("a project's rate becomes the fixed step", near(clock.getFixedStep(), 1.0f / 128.0f));

    // A hand-edited project.json is the reason for the bounds: a rate of zero
    // is a step of zero, and a step of zero is a tick loop that never drains.
    clock.setTickRate(0);
    check("  zero is clamped rather than dividing by nothing",
          clock.getFixedStep() > 0.0f
       && near(clock.getFixedStep(), 1.0f / static_cast<float>(Config::MIN_TICK_RATE)));

    clock.setTickRate(100000);
    check("  and an absurd rate is clamped too",
          near(clock.getFixedStep(), 1.0f / static_cast<float>(Config::MAX_TICK_RATE)));

    // The accumulator is a duration, so a faster project buys more ticks per
    // hitch rather than a longer stall.
    clock.setTickRate(64);
    const int budget = static_cast<int>(Config::MAX_FRAME_ACCUMULATOR / clock.getFixedStep());
    check("  the hitch budget is ticks, not seconds of stall", budget >= 16);
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

// No gameplay module is loaded here, so BehaviorRegistry is empty and every
// type name in a scene document is unknown - the state the editor is in after a
// failed build.
void testUnknownBehaviorsSurviveASave() {
    std::printf("A behavior whose module is missing:\n");

    const nlohmann::json authored = {
        {"behaviors", nlohmann::json::array({
            {{"type", "LabWalker"},
             {"properties", {{"speed", 6.5f}, {"jumpHeight", 1.25f}}}},
            {{"type", "SpinMe"}, {"properties", {{"rpm", 30.0f}}}},
        })}
    };

    ScriptComponent sc;
    ComponentSerializer::load(authored, sc);

    check("cannot be constructed", sc.behaviors.empty());
    check("  but is kept, not dropped", sc.unknown.size() == 2);

    // The property text is opaque here on purpose: the code that knows what
    // "speed" means is the module that is not loaded.
    const nlohmann::json again = ComponentSerializer::save(sc);
    check("  and saving writes it back unchanged", again == authored);

    // The list's order is the order the behaviors run in, so a held one has to
    // remember where it sat. Appending them all at the end round-trips
    // byte-identically here - every entry is unknown - and reorders the moment
    // one type in the list is registered and the rest are not.
    check("  remembering where each one sat",
          sc.unknown.size() == 2 && sc.unknown[0].index == 0 && sc.unknown[1].index == 1);

    // Recording the position is only half of it; the save has to read it back.
    // A list that is entirely held round-trips in order either way, so the
    // assertion has to put one out of order and watch it come back sorted -
    // which is what an implementation that appends cannot do.
    ScriptComponent shuffled;
    shuffled.unknown.push_back({"Second", "{}", 1});
    shuffled.unknown.push_back({"First",  "{}", 0});
    const nlohmann::json emitted = ComponentSerializer::save(shuffled);
    check("  and the save puts each one back at its own position",
          emitted["behaviors"].size() == 2
          && emitted["behaviors"][0]["type"] == "First"
          && emitted["behaviors"][1]["type"] == "Second");

    // The editor opens, saves, and opens again, so the loss to catch is the one
    // that only shows on the second pass.
    ScriptComponent reloaded;
    ComponentSerializer::load(again, reloaded);
    check("  through any number of saves", ComponentSerializer::save(reloaded) == authored);
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
    before.bones.push_back({3, EntityId{StorageIndex{7, 2}}, offset});
    before.bones.push_back({5, EntityId{StorageIndex{9, 1}}, glm::mat4(1.0f)});

    // A carrier naming entities by slot, which is what a scene file does, so a
    // reference that survives this round trip survives a save and load.
    auto bySlot   = [](EntityId e) { return e.slot(); };
    auto toSlotId = [](uint32_t slot) { return EntityId{StorageIndex{slot, 0}}; };
    const EntityNamer    name(bySlot);
    const EntityResolver resolve(toSlotId);

    Ragdoll after;
    ComponentSerializer::load(ComponentSerializer::save(before, name), after, resolve);

    check("a ragdoll keeps its switch", after.active);
    // The bodies are entities the scene saves anyway. Losing the mapping does
    // not save a ragdoll without its bodies - it saves one that has forgotten
    // them, beside a loose skeleton that falls.
    check("  and the bones that map bodies to it", after.bones.size() == 2);
    if (after.bones.size() == 2) {
        check("    naming the same bones", after.bones[0].bone == 3
                                        && after.bones[1].bone == 5);
        check("    and the same body slots", after.bones[0].body.slot() == 7
                                          && after.bones[1].body.slot() == 9);
        // Without this the body sits where the bone is, and every limb is
        // offset by half its own length the moment physics takes over.
        check("    keeping the offset from body to bone",
              near(after.bones[0].boneFromBody[3][1], 1.5f));
    }

    Joint joint;
    joint.type = JointType::Distance;
    joint.connected = EntityId{StorageIndex{4, 3}};
    joint.anchor = {0.0f, 0.5f, 0.0f};
    joint.distance = 2.5f;
    joint.stiffness = 0.4f;
    joint.collideConnected = true;

    Joint loaded;
    ComponentSerializer::load(ComponentSerializer::save(joint, name), loaded, resolve);

    check("a joint keeps its type", loaded.type == JointType::Distance);
    check("  its connected slot", loaded.connected.slot() == 4);
    check("  its anchor", near(loaded.anchor.y, 0.5f));
    check("  its length and give", near(loaded.distance, 2.5f)
                                && near(loaded.stiffness, 0.4f));
    check("  and whether the pair still collides", loaded.collideConnected);

    // A collider's points and its parts' spans into them, which is the whole
    // description of a mesh: lose either and the shape is empty.
    Collider collider;
    collider.parts.clear();
    // Padded so the span starts somewhere other than zero: a round-trip that
    // leaves meshFirst at its default cannot tell the field from its absence.
    collider.meshPoints = {{9,9,9}, {9,9,9}, {9,9,9}, {0,0,0}, {1,0,0}, {0,1,0}};
    ColliderPart part;
    part.shape = ColliderShape::Mesh;
    part.meshFirst = 3;
    part.meshCount = 3;
    collider.parts = { part };

    Collider back;
    ComponentSerializer::load(ComponentSerializer::save(collider), back);
    check("a mesh collider keeps its points", back.meshPoints.size() == 6);
    const bool span = back.parts.size() == 1
                   && back.parts[0].meshFirst == 3
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
    // Where the fallback puts it, not merely that it is a number: the buffer is
    // zero-filled, so isfinite() held for a bone the composer never touched.
    check("a bone whose body is gone falls back to its parent",
          near(second.global()[slice2 + 1][3][1], 1.3f));
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

// A ragdoll doubles as a hit box rig: its bones are bodies with colliders, so a
// query already picks one out. This is the whole path a game walks - shoot,
// find the limb, find whose it is.
// The branch made two components carry references to other entities, and taught
// the loader to recover them from the slots a save writes. Nothing tested the
// pair together: the component round-trips checked that a slot number survives,
// which is not the same as the reference still naming the entity it named.
void testSceneRoundTripKeepsReferences() {
    std::printf("A whole scene, written down and read back:\n");

    Scene scene;
    ResourceManager resources;

    // A jointed pair and a built ragdoll, which is every cross-entity reference
    // the branch added.
    const EntityId beam = scene.createEntity();
    scene.add<Transform>(beam, Transform{});
    scene.add(beam, makeName("Beam"));

    const EntityId weight = scene.createEntity();
    Transform at;
    at.position = {0.0f, -2.0f, 0.0f};
    scene.add<Transform>(weight, std::move(at));
    scene.add(weight, makeName("Weight"));
    Rigidbody body;
    body.mass = 5.0f;
    scene.add<Rigidbody>(weight, std::move(body));
    Joint rope;
    rope.type = JointType::Distance;
    rope.connected = beam;
    rope.distance = 2.0f;
    scene.add<Joint>(weight, std::move(rope));

    const EntityId rig = scene.createEntity();
    scene.add<Transform>(rig, Transform{});
    scene.add(rig, makeName("Rig"));
    check("a scene with a joint and a ragdoll in it",
          buildRagdoll(scene, rig, makeTestRig()) == 4);

    const size_t boneCount = scene.get<Ragdoll>(rig).bones.size();
    const std::string beamName = scene.get<Name>(beam).value;

    const std::string document = SceneSerializer::saveToString(scene, resources);
    check("  saves to a document", !document.empty());

    Scene back;
    ResourceManager backResources;
    check("  and loads back",
          SceneSerializer::loadFromString(document, back, backResources));

    // Found by name, because the entity ids are the loader's to choose.
    EntityId loadedWeight{};
    EntityId loadedRig{};
    back.forEachEntity([&](EntityId id) {
        if (!back.has<Name>(id)) return;
        const std::string name = back.get<Name>(id).value;
        if (name == "Weight") loadedWeight = id;
        if (name == "Rig")    loadedRig = id;
    });
    check("  the jointed body came back", loadedWeight && back.has<Joint>(loadedWeight));
    check("  the rig came back", loadedRig && back.has<Ragdoll>(loadedRig));

    // The part that a component round-trip cannot see: the reference has to name
    // a live entity, and the right one.
    const EntityId connected = back.get<Joint>(loadedWeight).connected;
    check("  and the joint still names something that exists",
          connected && back.isAlive(connected));
    check("    which is the entity it named before",
          back.has<Name>(connected) && beamName == back.get<Name>(connected).value);

    const Ragdoll& loadedRagdoll = back.get<Ragdoll>(loadedRig);
    check("  the ragdoll kept all of its bones", loadedRagdoll.bones.size() == boneCount);
    bool everyBoneAlive = loadedRagdoll.root && back.isAlive(loadedRagdoll.root);
    for (const RagdollBone& bone : loadedRagdoll.bones) {
        if (!bone.body || !back.isAlive(bone.body) || !back.has<Rigidbody>(bone.body)) {
            everyBoneAlive = false;
        }
    }
    check("    and every one of them is a live body", everyBoneAlive);
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

} // namespace

// A world's simulation must be a function of the world, not of the order it was
// assembled in. SparseSet is dense and swap-and-pop, so a storage walk carries
// the history of every insertion and removal - and both Gauss-Seidel loops in a
// tick are order-dependent. Physics canonicalises its two lists on entity slot
// and says so where it does it; this is the test that says so too, because a
// comment cannot fail.
void testAConversationKnowsWhatArrived() {
    std::printf("What each end learns from traffic it was sending anyway:\n");

    NetConnection alice, bob;
    alice.open(NetAddress{0x7F000001u, 40001});
    bob.open(NetAddress{0x7F000001u, 40002});

    std::vector<uint8_t> wire, got;
    std::vector<uint16_t> acknowledged;
    const uint8_t hello[] = {7, 7, 7};

    alice.frame(hello, sizeof(hello), wire);
    check("a framed packet carries its payload after the header",
          wire.size() == NetConnection::HEADER_BYTES + sizeof(hello));
    check("and bob takes it", bob.accept(wire.data(), wire.size(), got, acknowledged));
    check("with the payload intact", got.size() == 3 && got[0] == 7);

    // The same datagram arriving twice - a duplicate, or a retransmit by some
    // middlebox. Applying it again would be applying a moment already applied.
    check("the same packet twice is refused the second time",
          !bob.accept(wire.data(), wire.size(), got, acknowledged));

    // Out of order. Alice sends three, bob gets the third before the second.
    std::vector<uint8_t> second, third;
    alice.frame(hello, sizeof(hello), second);
    alice.frame(hello, sizeof(hello), third);
    check("the newer of two arrives", bob.accept(third.data(), third.size(), got, acknowledged));
    check("and the older behind it is refused, not applied backwards",
          !bob.accept(second.data(), second.size(), got, acknowledged));

    // Bob answers, and his answer carries what he has seen. Alice reads her own
    // round trip out of it without either end sending a ping.
    alice.advance(0.030f);
    bob.frame(nullptr, 0, wire);
    check("alice takes bob's answer", alice.accept(wire.data(), wire.size(), got, acknowledged));
    check("an empty payload is a legal packet", got.empty());
    check("and alice now knows the trip took about thirty milliseconds",
          std::abs(alice.roundTrip() - 0.030f) < 0.001f);

    // Silence. Neither timer is about the game's clock - they are wall time, so
    // a peer that stops answering is noticed even while the world is paused.
    check("a fresh connection is not timed out", !bob.timedOut());
    bob.advance(NetConnection::TIMEOUT_SECONDS + 0.1f);
    check("but one that has heard nothing for the timeout is", bob.timedOut());

}

void testSequencesSurviveTheirOwnWrap() {
    std::printf("Sixteen bits run out after about eighteen minutes at sixty a second:\n");

    // The interesting packet is not the first, it is the one after 65535. If
    // newer-than is a plain comparison the connection stops accepting anything
    // at that point and the game dies at a fixed time after it started.
    NetConnection sender, receiver;
    sender.open(NetAddress{0x7F000001u, 1});
    receiver.open(NetAddress{0x7F000001u, 2});

    std::vector<uint8_t> wire, got;
    std::vector<uint16_t> acknowledged;
    size_t accepted = 0;
    for (int i = 0; i < 70000; ++i) {
        sender.frame(nullptr, 0, wire);
        if (receiver.accept(wire.data(), wire.size(), got, acknowledged)) ++accepted;
    }
    check("every packet across the wrap is accepted", accepted == 70000);
    std::printf("      %zu packets, sequence wrapped %d time(s)\n", accepted, 70000 / 65536);
}

void testTheSchemaCarriesComponentsThroughAScene() {
    std::printf("What a registered component costs, and what survives the trip:\n");

    NetSchema schema;
    schema.replicate<Transform>("Transform");
    schema.replicate<Rigidbody>("Rigidbody");

    check("both types registered", schema.size() == 2);
    check("and are found by name", schema.indexOf("Transform") == 0 &&
                                   schema.indexOf("Rigidbody") == 1);
    check("a name nobody registered is not found", schema.indexOf("Health") < 0);

    Scene sender;
    const EntityId crate = sender.createEntity();
    Transform placed;
    placed.position = {12.5f, 3.25f, -40.125f};
    placed.rotation = glm::angleAxis(glm::radians(37.0f), glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
    sender.add(crate, placed);
    Rigidbody moving;
    moving.linearVelocity  = {2.0f, -9.8f, 0.5f};
    moving.angularVelocity = {0.0f, 1.5f, 0.0f};
    sender.add(crate, moving);

    std::vector<uint8_t> packet;
    BitWriter writer(packet, 256);
    for (const NetType& type : schema.types()) {
        check("the sender has this component to write", type.encode(sender, crate, writer));
    }
    writer.finish();

    // The receiving world has the entity and neither component - which is what
    // a client looks like when a body it has never been told about starts
    // moving. Decoding has to build them, not refuse.
    Scene receiver;
    const EntityId mirror = receiver.createEntity();
    check("the two worlds agree on the slot", mirror.slot() == crate.slot());
    check("and the receiver starts without the components",
          !receiver.has<Transform>(mirror) && !receiver.has<Rigidbody>(mirror));

    BitReader reader(packet.data(), packet.size());
    for (const NetType& type : schema.types()) {
        check("the receiver takes it", type.decode(receiver, mirror, reader));
    }
    check("and the whole read landed", !reader.failed());

    const Transform& got = receiver.get<Transform>(mirror);
    const Rigidbody& body = receiver.get<Rigidbody>(mirror);
    check("the position arrived inside a millimetre",
          glm::length(got.position - placed.position) < 0.002f);
    check("the rotation arrived inside a fifth of a degree",
          rotationErrorDegrees(placed.rotation, got.rotation) < 0.2f);
    check("an unscaled body spent one bit on its scale, not ninety-six",
          glm::length(got.scale - glm::vec3(1.0f)) < 1e-6f);
    check("the velocity arrived inside a centimetre a second",
          glm::length(body.linearVelocity - moving.linearVelocity) < 0.02f);

    std::printf("      transform and body together: %zu bytes\n", packet.size());

    // A body at rest is the common case in a settled world, and it is the one
    // that has to be cheap or a tower of a hundred crates costs a packet a tick
    // forever after it stops moving.
    Scene still;
    const EntityId resting = still.createEntity();
    still.add<Rigidbody>(resting, Rigidbody{});
    std::vector<uint8_t> restingPacket;
    BitWriter restingWriter(restingPacket, 64);
    schema.types()[1].encode(still, resting, restingWriter);
    check("a body at rest costs three bits", restingWriter.bitCount() == 3);
}

void testTwoEndsRefuseToPlayDifferentGames() {
    std::printf("What the handshake catches before it becomes a decoded-wrong world:\n");

    NetSchema server, client;
    server.replicate<Transform>("Transform");
    server.replicate<Rigidbody>("Rigidbody");
    client.replicate<Transform>("Transform");
    client.replicate<Rigidbody>("Rigidbody");
    check("two ends built from the same source agree",
          server.fingerprint() == client.fingerprint());

    // The same names in the other order. Every name matches and nothing else
    // does, because the index is the wire identity - so this must not compare
    // equal, and a fingerprint over an unordered set would say it does.
    NetSchema swapped;
    swapped.replicate<Rigidbody>("Rigidbody");
    swapped.replicate<Transform>("Transform");
    check("the same names registered in a different order do not",
          server.fingerprint() != swapped.fingerprint());

    NetSchema extra;
    extra.replicate<Transform>("Transform");
    extra.replicate<Rigidbody>("Rigidbody");
    extra.replicate<Transform>("Health");
    check("an end with a component the other has not heard of does not",
          server.fingerprint() != extra.fingerprint());

    NetSchema owned;
    owned.replicate<Transform>("Transform");
    owned.replicate<Rigidbody>("Rigidbody", NetPolicy::OwnerOnly);
    check("and neither does the same list sent to different people",
          server.fingerprint() != owned.fingerprint());

    check("the description names what is registered",
          server.describe() == std::string("Transform, Rigidbody"));
    check("and says who owner-only rows go to",
          owned.describe() == std::string("Transform, Rigidbody (owner)"));
}

namespace {

/**
 * @brief Children of @p entity, walked off the hierarchy's own linked list.
 *
 * Local to the tests: the engine has no caller for it.
 */
int childrenOf(const Scene& scene, EntityId entity) {
    if (!scene.isAlive(entity) || !scene.has<Hierarchy>(entity)) return 0;
    int count = 0;
    for (EntityId at = scene.get<Hierarchy>(entity).firstChild;
         at && scene.isAlive(at);
         at = scene.get<Hierarchy>(at).nextSibling) {
        ++count;
    }
    return count;
}

/**
 * @brief readSnapshot reads from a stream; the tests always have a whole body, so this
 * wraps the one-line reader they would otherwise each build.
 */
bool readSnapshotFrom(Scene& scene, const NetSchema& schema, const std::vector<uint8_t>& body) {
    BitReader reader(body.data(), body.size());
    return readSnapshot(scene, schema, reader);
}

/**
 * @brief A world of @p count crates in a line, each with a body.
 *
 * Returns their ids rather than letting a caller guess slots: slot zero is the reserved
 * null, so the first crate is not entity zero.
 */
std::vector<EntityId> buildCrates(Scene& scene, int count) {
    std::vector<EntityId> crates;
    for (int i = 0; i < count; ++i) {
        const EntityId crate = scene.createEntity();
        Transform at;
        at.position = {static_cast<float>(i) * 1.5f, 4.0f, 0.0f};
        scene.add(crate, at);
        scene.add(crate, Rigidbody{});
        crates.push_back(crate);
    }
    return crates;
}

/**
 * @brief Fill @p schema with what the engine replicates.
 *
 * Filled rather than returned, because a schema is not copyable - two of them would be
 * two wire orders.
 */
void fillWorldSchema(NetSchema& schema) {
    schema.replicate<Transform>("Transform");
    schema.replicate<Rigidbody>("Rigidbody");
    schema.replicate<Ragdoll>("Ragdoll");
}

} // namespace

void testTheCommonestValuesSurviveExactly() {
    std::printf("What a quantiser owes the value it will be handed most:\n");

    // Identity and rest are not ordinary values. Almost everything in a scene
    // is unrotated and almost everything in it is still, so an encoding that
    // cannot say either is wrong about most of the world most of the time.
    std::vector<uint8_t> packet;
    BitWriter writer(packet, 64);
    Quantize::writeRotation(writer, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    Quantize::writeVelocity(writer, 0.0f);
    writer.finish();

    BitReader reader(packet.data(), packet.size());
    const glm::quat back  = Quantize::readRotation(reader);
    const float     still = Quantize::readVelocity(reader);

    check("an unrotated body comes back unrotated, exactly",
          rotationErrorDegrees(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), back) < 1e-4f);
    check("and a still one comes back still, exactly", still == 0.0f);

    // Why exactly matters, and it is not tidiness. A rotation error is an
    // angle, so what it costs on the ground grows with the size of the thing
    // turned. The lab's floor is 62 m across; a quarter of a degree of error
    // lifts one end of it and drops the other by a tenth of a metre, and a
    // character standing on the low end finds nothing under it.
    float worst = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const float angle = glm::radians(static_cast<float>(i) * 0.9f);
        const glm::quat q = glm::angleAxis(angle, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));

        std::vector<uint8_t> one;
        BitWriter w(one, 32);
        Quantize::writeRotation(w, q);
        w.finish();
        BitReader r(one.data(), one.size());
        worst = std::max(worst, rotationErrorDegrees(q, Quantize::readRotation(r)));
    }
    check("no rotation is out by more than a tenth of a degree", worst < 0.1f);

    const float acrossTheFloor = 31.0f * std::sin(glm::radians(worst));
    std::printf("      worst rotation error %.4f deg, which is %.1f mm at the "
                "far corner of the lab's floor\n",
                static_cast<double>(worst), static_cast<double>(acrossTheFloor) * 1000.0);
    check("so the far corner of a sixty-metre floor moves by millimetres, not centimetres",
          acrossTheFloor < 0.05f);

    // And a body that cannot move is not described at all, which is the other
    // half of the same answer: the scene file is exact where the wire is not.
    Scene scene;
    const EntityId ground = scene.createEntity();
    scene.add(ground, Transform{});
    Rigidbody stone;
    stone.isStatic = true;
    scene.add(ground, stone);
    check("a static body is not put on the wire", isImmovable(scene, ground));

    const EntityId crate = scene.createEntity();
    scene.add(crate, Transform{});
    scene.add(crate, Rigidbody{});
    check("while one that can move still is", !isImmovable(scene, crate));
}

void testABoneTheAnimationPlacesIsNotWorthAPacket() {
    std::printf("What a character costs when the animation already says it:\n");

    NetSchema schema;
    fillWorldSchema(schema);

    Scene server;
    addBox(server, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});

    const EntityId rig = server.createEntity();
    server.add<Transform>(rig, Transform{});
    const SkeletonAsset skeleton = makeTestRig();
    const uint32_t bones = buildRagdoll(server, rig, skeleton);
    check("the rig has bones to argue about", bones > 0);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    // Inactive: RagdollSystem places every bone from the animated pose, on both
    // ends, from a clip chosen by a velocity that does replicate. So the bones
    // are worked out rather than told, and none of them belongs on the wire.
    check("the ragdoll starts inactive", !server.get<Ragdoll>(rig).active);

    const NetSnapshotStats quiet = writeSnapshot(server, schema, 1, budget, baseline, body);
    for (const RagdollBone& bone : server.get<Ragdoll>(rig).bones) {
        check("  a bone the animation places is not described",
              isPosedByAnimation(server, bone.body));
    }
    const uint32_t withoutBones = quiet.entitiesWritten;
    std::printf("      inactive: %u entities on the wire, %u bones held back\n",
                withoutBones, bones);

    // Active: the solver drives the bones and the pose follows, so now they are
    // the answer rather than a copy and every one travels. A client that missed
    // the switch would pose a walk cycle over a body that is falling.
    server.get<Ragdoll>(rig).active = true;
    for (const RagdollBone& bone : server.get<Ragdoll>(rig).bones) {
        check("  a bone the solver drives is described",
              !isPosedByAnimation(server, bone.body));
    }

    const NetSnapshotStats loud = writeSnapshot(server, schema, 2, budget, baseline, body);
    check("switching physics on puts the whole skeleton on the wire",
          loud.entitiesWritten >= withoutBones + bones);
    std::printf("      active:   %u entities on the wire\n", loud.entitiesWritten);

    // And the switch itself travels, which is what makes the two states agree.
    baseline.confirm(2);
    Scene client;
    check("the client takes it", readSnapshotFrom(client, schema, body));
    const EntityId mirror = client.entityAt(rig.slot());
    check("the client learned the ragdoll is active",
          client.isAlive(mirror) && client.has<Ragdoll>(mirror)
          && client.get<Ragdoll>(mirror).active);
}

void testTheFirstSnapshotIsTheWholeWorld() {
    std::printf("Joining and playing are the same code path:\n");

    NetSchema schema;
    fillWorldSchema(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 40);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    const NetSnapshotStats first = writeSnapshot(server, schema, 1, budget, baseline, body);
    check("a connection that has confirmed nothing is sent everything",
          first.entitiesWritten == 40);
    check("with no separate mode for it", first.entitiesDeferred == 0);

    Scene client;
    check("and the client takes it", readSnapshotFrom(client, schema, body));
    check("building every entity it had never heard of", client.entityCount() == 40);

    const EntityId last = client.entityAt(crates.back().slot());
    check("at the position the server had it",
          std::abs(client.get<Transform>(last).position.x - 39 * 1.5f) < 0.002f);

    // Nothing moved, and the client confirmed. The second snapshot must be
    // silent: this is what makes a settled world of any size free, and without
    // it a tower of crates costs a packet a tick forever after it stops.
    baseline.confirm(1);
    const NetSnapshotStats second = writeSnapshot(server, schema, 2, budget, baseline, body);
    check("a world that has not changed sends nothing", second.entitiesWritten == 0);
    std::printf("      whole world %u B, then %u B once it settled\n",
                first.bytesWritten, second.bytesWritten);
}

void testALostSnapshotIsSaidAgain() {
    std::printf("The bug that freezes a body on one client for the rest of a match:\n");

    NetSchema schema;
    fillWorldSchema(schema);
    Scene server;
    const EntityId crate = buildCrates(server, 4).front();

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    writeSnapshot(server, schema, 1, budget, baseline, body);
    baseline.confirm(1);

    Scene client;
    readSnapshotFrom(client, schema, body);

    // The crate moves, the snapshot saying so is lost, then it stops. Nothing
    // about it differs again - so a server that folded that snapshot in when it
    // wrote it leaves the crate at the old position until the match ends.
    server.get<Transform>(crate).position = {99.0f, 1.0f, 2.0f};
    const NetSnapshotStats moved = writeSnapshot(server, schema, 2, budget, baseline, body);
    check("the move is written", moved.entitiesWritten == 1);

    // Snapshot 2 is never confirmed - it did not arrive.
    const NetSnapshotStats again = writeSnapshot(server, schema, 3, budget, baseline, body);
    check("and while it is unconfirmed the move is written again",
          again.entitiesWritten == 1);

    readSnapshotFrom(client, schema, body);
    baseline.confirm(3);
    check("so the client ends up where the server put it",
          std::abs(client.get<Transform>(client.entityAt(crate.slot())).position.x - 99.0f) < 0.002f);

    const NetSnapshotStats quiet = writeSnapshot(server, schema, 4, budget, baseline, body);
    check("and only then does the server stop saying it", quiet.entitiesWritten == 0);
}

void testAnAcknowledgementOutOfOrderDoesNotUndoANewerOne() {
    std::printf("Acknowledgements arrive in whatever order the network chose:\n");

    NetSchema schema;
    fillWorldSchema(schema);
    Scene server;
    const EntityId crate = buildCrates(server, 1).front();

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    server.get<Transform>(crate).position = {1.0f, 0.0f, 0.0f};
    writeSnapshot(server, schema, 10, budget, baseline, body);
    server.get<Transform>(crate).position = {2.0f, 0.0f, 0.0f};
    writeSnapshot(server, schema, 11, budget, baseline, body);

    // The newer arrives first, then the older. Folding the older in afterwards
    // would leave the server believing the client holds x=1, and it would then
    // resend x=2 forever - or worse, believe x=2 was already delivered.
    baseline.confirm(11);
    baseline.confirm(10);

    const NetSnapshotStats after = writeSnapshot(server, schema, 12, budget, baseline, body);
    check("the older acknowledgement does not overwrite the newer",
          after.entitiesWritten == 0);
}

void testABurstIsDeferredRatherThanDropped() {
    std::printf("What a collapsing tower does to a packet:\n");

    NetSchema schema;
    fillWorldSchema(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 117);

    NetBaseline baseline;
    NetBudget budget;
    budget.owner = crates.front();
    std::vector<uint8_t> body;

    // Every body awake and moving - the frame the old attempt measured at 8455
    // bytes and 643 consecutive refusals, where the client received nothing at
    // all at the exact moment there was most to see.
    server.forEachEntity([&](EntityId entity) {
        server.get<Rigidbody>(entity).linearVelocity  = {3.0f, -9.0f, 1.5f};
        server.get<Rigidbody>(entity).angularVelocity = {0.0f, 2.0f, 0.0f};
    });

    Scene client;
    uint16_t sequence = 1;
    int snapshots = 0;
    uint32_t worst = 0;

    // Drive it until every body has arrived, confirming each snapshot.
    while (snapshots < 20) {
        const NetSnapshotStats stats = writeSnapshot(server, schema, sequence, budget, baseline, body);
        worst = std::max(worst, stats.bytesWritten);
        check("no snapshot exceeds what a datagram can carry", stats.bytesWritten <= budget.bytes);
        readSnapshotFrom(client, schema, body);
        baseline.confirm(sequence);
        ++sequence;
        ++snapshots;
        // The connection's own entity is written every snapshot whether it
        // moved or not, so "nothing to say" is one entry rather than none.
        if (stats.entitiesWritten <= 1) break;
    }

    check("every body reached the client", client.entityCount() == 117);
    check("and it took a handful of snapshots, not a hundred", snapshots <= 6);
    std::printf("      117 moving bodies delivered in %d snapshots, worst %u B\n",
                snapshots, worst);

    // The owner is the anchor reconciliation compares against, so it must never
    // be the entity that gets deferred.
    NetBaseline fresh;
    const NetSnapshotStats first = writeSnapshot(server, schema, 100, budget, fresh, body);
    check("the burst does not fit one snapshot", first.entitiesDeferred > 0);

    bool ownerPresent = false;
    Scene probe;
    readSnapshotFrom(probe, schema, body);
    ownerPresent = probe.isAliveAtIndex(budget.owner.slot());
    check("and the owner is in it anyway", ownerPresent);
}

void testAnEntityThatLeavesTheWorldLeavesEveryClient() {
    std::printf("What a client is told about an entity that is gone:\n");

    NetSchema schema;
    fillWorldSchema(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 6);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    writeSnapshot(server, schema, 1, budget, baseline, body);
    baseline.confirm(1);
    Scene client;
    readSnapshotFrom(client, schema, body);
    check("six crates to start", client.entityCount() == 6);

    const uint32_t goneSlot = crates[2].slot();
    server.destroyEntity(crates[2]);
    const NetSnapshotStats told = writeSnapshot(server, schema, 2, budget, baseline, body);
    check("the server reports the one that went", told.entitiesLost == 1);

    // Lost before it was confirmed, so it must be reported again - destruction
    // has to be reliable or the client keeps a body nobody else can see.
    const NetSnapshotStats retold = writeSnapshot(server, schema, 3, budget, baseline, body);
    check("and keeps reporting it until the client confirms", retold.entitiesLost == 1);

    readSnapshotFrom(client, schema, body);
    baseline.confirm(3);
    check("the client destroyed it", client.entityCount() == 5);
    check("and it was the right one", !client.isAliveAtIndex(goneSlot));

    const NetSnapshotStats quiet = writeSnapshot(server, schema, 4, budget, baseline, body);
    check("only then does the server stop mentioning it", quiet.entitiesLost == 0);
}

void testOwnerOnlyStateGoesToItsOwnerAlone() {
    std::printf("State that exists to make one player's prediction converge:\n");

    NetSchema schema;
    schema.replicate<Transform>("Transform");
    schema.replicate<Rigidbody>("Rigidbody", NetPolicy::OwnerOnly);

    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 3);
    server.forEachEntity([&](EntityId entity) {
        server.get<Rigidbody>(entity).linearVelocity = {5.0f, 0.0f, 0.0f};
    });

    std::vector<uint8_t> body;
    NetBaseline mine, theirs;
    NetBudget forOwner;
    forOwner.owner = crates[1];
    NetBudget forOther;
    forOther.owner = crates[0];

    writeSnapshot(server, schema, 1, forOwner, mine, body);
    Scene ownerView;
    readSnapshotFrom(ownerView, schema, body);

    writeSnapshot(server, schema, 1, forOther, theirs, body);
    Scene otherView;
    readSnapshotFrom(otherView, schema, body);

    const uint32_t ownedSlot = crates[1].slot();
    check("the owner is told its own body's velocity",
          ownerView.has<Rigidbody>(ownerView.entityAt(ownedSlot)));
    check("and nobody else is",
          !otherView.has<Rigidbody>(otherView.entityAt(ownedSlot)));
    check("but everyone still sees where it is",
          std::abs(otherView.get<Transform>(otherView.entityAt(ownedSlot)).position.x - 1.5f) < 0.002f);
}

namespace {

/**
 * @brief The engine's own wire types, registered for one test and gone after it.
 *
 * The schema is process-global. A test that fills it has to leave it empty for
 * the next one, and doing that by hand means a line on every path out - which
 * the early returns of a failing test do not take.
 */
class ScopedEngineSchema {
    public:
        ScopedEngineSchema() {
            NetSchema::get().clear();
            registerEngineNetTypes();
        }
        ~ScopedEngineSchema() { NetSchema::get().clear(); }

        ScopedEngineSchema(const ScopedEngineSchema& other) = delete;
        ScopedEngineSchema& operator=(const ScopedEngineSchema& other) = delete;

        ScopedEngineSchema(ScopedEngineSchema && other) = delete;
        ScopedEngineSchema& operator=(ScopedEngineSchema && other) = delete;
};

/**
 * @brief A body a rule moves, rather than a solver.
 *
 * Kinematic on purpose: these tests are about what the wire carries and what
 * the two ends agree on, so the motion has to come from the test rather than
 * from physics deciding something interesting.
 */
EntityId addWalker(Scene& scene) {
    const EntityId walker = scene.createEntity();
    Transform at;
    scene.add(walker, at);
    Rigidbody body;
    body.isKinematic = true;
    scene.add(walker, body);
    return walker;
}

InputCommand walkCommand(uint32_t sequence, uint32_t tick, float forward, bool jump) {
    InputCommand command;
    command.sequence = sequence;
    command.tick     = tick;
    command.axis[0]  = forward;
    command.axis[1]  = 0.0f;
    if (jump) command.pressed = 1u << 2;
    command.view = glm::angleAxis(glm::radians(static_cast<float>(tick)), glm::vec3(0.0f, 1.0f, 0.0f));
    return command;
}

constexpr uint32_t TEST_ACTIONS = 6;

} // namespace

void testAJumpSurvivesTheNetworkLosingIt() {
    std::printf("An axis recovers on its own; an edge does not:\n");

    // The player jumps on tick 100 and keeps walking. Every packet after it
    // repeats that command, so the jump has twelve chances to arrive.
    std::vector<InputCommand> sent;
    for (uint32_t i = 0; i < 20; ++i) {
        sent.push_back(walkCommand(i, 100 + i, 1.0f, i == 0));
    }

    NetCommandBuffer server;
    std::vector<uint8_t> packet;
    std::vector<InputCommand> got;

    // Drop the first eight packets outright - a burst of loss covering the
    // packet the jump was first sent in and seven after it.
    int delivered = 0;
    for (size_t frame = 1; frame <= sent.size(); ++frame) {
        const size_t oldest = frame > NET_COMMAND_REDUNDANCY
                            ? frame - NET_COMMAND_REDUNDANCY : 0;
        const std::vector<InputCommand> window(
            sent.begin() + static_cast<long>(oldest),
            sent.begin() + static_cast<long>(frame));

        BitWriter writer(packet, 512);
        writeCommands(writer, window, TEST_ACTIONS);
        writer.finish();
        if (frame <= 8) continue;

        BitReader reader(packet.data(), packet.size());
        check("the packet decodes", readCommands(reader, TEST_ACTIONS, got));
        for (const InputCommand& command : got) server.accept(command);
        ++delivered;
    }
    check("packets did arrive eventually", delivered == 12);

    // The server now runs the ticks in order, as it does in the frame loop.
    // The jump has to be in tick 100 - it was pressed, and the player is owed
    // it - and it has to be in exactly one tick. Every one of those twelve
    // packets carried a copy, so a server that ran what arrived rather than
    // what it had not yet run would jump twelve times from one keypress.
    int jumps = 0;
    InputCommand ran;
    for (uint32_t tick = 100; tick < 120; ++tick) {
        const InputCommand command = server.take(tick);
        if (tick == 100) ran = command;
        if (command.pressed & (1u << 2)) ++jumps;
    }
    check("the jump pressed eight packets ago is still there", ran.pressed == (1u << 2));
    check("on the tick it was pressed on", ran.tick == 100);
    check("with the axis it was pressed with", std::abs(ran.axis[0] - 1.0f) < 0.01f);
    check("and once, not once per copy that arrived", jumps == 1);
}

void testATickWithNoCommandStillRuns() {
    std::printf("What the server does when the next command has not arrived:\n");

    NetCommandBuffer server;
    server.accept(walkCommand(1, 50, 1.0f, true));

    const InputCommand first = server.take(50);
    check("the command runs on the tick it names", first.tick == 50);
    check("with its edge", first.pressed != 0);

    // Tick 51's command is still in flight. The server cannot wait for it.
    const InputCommand gap = server.take(51);
    check("the next tick runs anyway", gap.tick == 51);
    check("holding the axis, because the key is still held",
          std::abs(gap.axis[0] - 1.0f) < 0.01f);
    check("but not the edge, which already fired", gap.pressed == 0);

    const InputCommand secondGap = server.take(52);
    check("and it does not fire on the tick after that either", secondGap.pressed == 0);
}

void testCommandsRunInTheOrderTheyWereMade() {
    std::printf("One command, one tick, in order, once:\n");

    NetCommandBuffer server;

    // Three commands arrive together, as they do inside one packet.
    server.accept(walkCommand(1, 200, 0.5f, false));
    server.accept(walkCommand(2, 201, 0.6f, false));
    server.accept(walkCommand(3, 202, 0.7f, false));
    check("all three are waiting", server.pending() == 3);

    // One per tick, oldest first. The client predicted each of its ticks from
    // one of these in this order, so the server must run them the same way or
    // the two ends compute different answers from the same input.
    check("the first tick runs the first command",
          std::abs(server.take(900).axis[0] - 0.5f) < 0.01f);
    check("the next runs the next",
          std::abs(server.take(901).axis[0] - 0.6f) < 0.01f);
    check("and the next the next",
          std::abs(server.take(902).axis[0] - 0.7f) < 0.01f);
    check("with nothing left waiting", server.pending() == 0);

    // The sender's own numbering is kept, because that is what a snapshot hands
    // back so the client knows which prediction has been judged.
    check("the sender's number for what ran is remembered",
          server.newestRunTick() == 202);

    // Nothing waiting, and the tick has to run anyway. The command carries the
    // tick it is running on, not the one the sender was on: a system reading it
    // is in the server's tick, not the client's.
    check("a command runs stamped with the tick it is running on",
          server.take(903).tick == 903);

    // A tick moved on repeated input, so a moment of the client's was lived
    // without its say. Claiming it keeps the snapshot honest: what it confirms
    // describes the pose it carries, not one a tick further on.
    check("and a repeat claims the moment it stood in for",
          server.newestRunTick() == 203);

    // A late copy of a command already run. Running it again would run the
    // player's input twice from one keypress.
    server.accept(walkCommand(1, 200, 0.5f, false));
    check("a copy of a command already run is not queued again",
          server.pending() == 0);

    // And the real command for the moment a repeat covered. Late rather than
    // duplicate - it was never run - but the moment is spent either way, and
    // running it now would move the character twice for one moment of input.
    server.accept(walkCommand(4, 203, 0.9f, true));
    check("nor is the command a repeat already stood in for",
          server.pending() == 0);

    // Its edges survive it, though. An axis says where the stick is now and the
    // next command says it again; a press is true once, and dropping it is a
    // jump the player asked for and never got.
    server.accept(walkCommand(5, 204, 0.9f, false));
    check("but the press it carried is folded into the next that runs",
          server.take(904).pressed != 0);
}

void testWhatAFrameOfInputCostsOnTheWire() {
    std::printf("What input costs, with every copy of itself it carries:\n");

    std::vector<InputCommand> window;
    for (uint32_t i = 0; i < NET_COMMAND_REDUNDANCY; ++i) {
        window.push_back(walkCommand(i, 1000 + i, i % 2 ? 1.0f : -1.0f, i == 3));
    }

    std::vector<uint8_t> packet;
    BitWriter writer(packet, 512);
    writeCommands(writer, window, TEST_ACTIONS);
    writer.finish();

    std::vector<InputCommand> got;
    BitReader reader(packet.data(), packet.size());
    check("a full packet of commands decodes", readCommands(reader, TEST_ACTIONS, got));
    check("all of them", got.size() == NET_COMMAND_REDUNDANCY);
    check("and the read landed exactly", !reader.failed());

    bool same = true;
    for (size_t i = 0; i < got.size(); ++i) {
        same = same && got[i].sequence == window[i].sequence
                    && got[i].tick == window[i].tick
                    && got[i].pressed == window[i].pressed
                    && std::abs(got[i].axis[0] - window[i].axis[0]) < 0.01f
                    && rotationErrorDegrees(window[i].view, got[i].view) < 0.3f;
    }
    check("each one carrying what it was given", same);

    std::printf("      %zu commands, %zu bits each, %zu bytes a packet, %.1f KB/s up\n",
                window.size(), writer.bitCount() / window.size(), packet.size(),
                packet.size() * NET_COMMAND_RATE / 1024.0);

    // What matters is not how small it is but that a whole frame of input, with
    // every copy it carries, never approaches a datagram - else the redundancy
    // protecting a jump would be what lost the packet carrying it.
    check("a frame of input is nowhere near a datagram",
          packet.size() < UdpSocket::MAX_DATAGRAM / 3);
}

namespace {

/**
 * @brief Pump both ends until @p done, or give up.
 *
 * Real sockets on loopback deliver within a frame, so a handful of rounds is generous;
 * the cap is there so a broken build fails the test instead of hanging the suite.
 */
template <typename Step, typename Done>
bool pumpUntil(Step step, Done done, int rounds = 40) {
    for (int i = 0; i < rounds; ++i) {
        step();
        if (done()) return true;
    }
    return false;
}

} // namespace

void testTwoSessionsPlayTheSameGame() {
    std::printf("A server and a client, on real sockets, joining and playing:\n");

    const ScopedEngineSchema wire;

    ResourceManager resources;
    Scene serverWorld;
    const std::vector<EntityId> crates = buildCrates(serverWorld, 30);

    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId id) {
            const EntityId player = scene.createEntity();
            Transform at;
            at.position = {static_cast<float>(id) * 3.0f, 1.0f, -5.0f};
            scene.add(player, at);
            scene.add(player, Rigidbody{});
            return player;
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId entity) { scene.destroyEntity(entity); });

    check("the server binds a port", server.host(0, 4));
    check("and is refereeing straight away", server.isPlaying());
    check("a host drives nobody, so every player is a real client",
          !server.localEntity());

    Scene clientWorld;
    NetSession client;
    check("the client opens a socket", client.connect(NetAddress{0x7F000001u, server.localAddress().port}));
    check("but is not playing until it is welcomed", !client.isPlaying());

    // Three actions, as a project defines. Both ends agree because both run
    // the same project.
    constexpr uint32_t ACTIONS = 3;

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, ACTIONS);
        client.beginTick(tick, InputCommand{}, ACTIONS);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(0.016f);
        client.advance(0.016f);
    };

    check("the client is welcomed", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and given a player number", client.localPlayer() != NO_PLAYER);
    check("and an entity to drive", clientWorld.isAlive(client.localEntity()));

    check("the server counts the one player in it", server.playerCount() == 1);
    check("and still owns nothing itself", !server.localEntity());

    check("the world arrives",
          pumpUntil(frame, [&]() { return clientWorld.entityCount() >= 31; }, 80));
    std::printf("      %zu entities on the server, %zu on the client\n",
                serverWorld.entityCount(), clientWorld.entityCount());

    // The crates the server built are where the server has them.
    const EntityId probe = crates[17];
    const EntityId mirror = clientWorld.entityAt(probe.slot());
    check("a crate the client was never told about directly is there",
          clientWorld.isAlive(mirror));
    check("at the position the server has it",
          std::abs(clientWorld.get<Transform>(mirror).position.x
                   - serverWorld.get<Transform>(probe).position.x) < 0.002f);

    // The server moves something. It has to reach the client without anything
    // being asked for.
    serverWorld.get<Transform>(probe).position = {-77.5f, 12.25f, 3.0f};
    check("a change on the server reaches the client",
          pumpUntil(frame, [&]() {
              return std::abs(clientWorld.get<Transform>(mirror).position.x + 77.5f) < 0.002f;
          }));

    // Ownership: the client predicts its own entity and nothing else, which is
    // what stops it shoving every crate its own way and being corrected.
    check("the client simulates what it owns", client.simulates(client.localEntity()));
    check("and not the crates", !client.simulates(mirror));
    check("and knows which is its own", client.isMine(client.localEntity()));
    check("the server simulates everything", server.simulates(probe));
    check("but owns nothing itself", !server.isMine(probe));

    // Input goes the other way, and the server runs it on the tick it names.
    const uint32_t inputTick = tick + 1;
    InputCommand walking;
    walking.sequence = 500;
    walking.tick     = inputTick;
    walking.axis[0]  = 1.0f;    // move_forward, held
    walking.pressed  = 1u << 2; // jump, this tick only
    client.beginTick(inputTick, walking, ACTIONS);
    client.send(clientWorld, inputTick);
    server.receive(serverWorld, resources);
    server.beginTick(inputTick, InputCommand{}, ACTIONS);

    const EntityId playerOnServer = server.players().front().entity;
    check("the server knows the player it is refereeing", bool(playerOnServer));
    const InputCommand& ran = server.commandFor(playerOnServer);
    check("the server runs the client's input on the client's entity",
          std::abs(ran.axis[0] - 1.0f) < 0.01f);
    check("with the edge intact", ran.pressed == (1u << 2));
    check("and nothing for an entity no player drives",
          server.commandFor(probe).pressed == 0);

    // Leaving is said, not waited for.
    client.close();
    server.receive(serverWorld, resources);
    for (int i = 0; i < 3; ++i) { server.advance(2.0f); server.receive(serverWorld, resources); }
    check("the seat is freed when a player leaves", server.playerCount() == 0);

    server.close();
}

void testAClientBuiltFromDifferentSourceIsTurnedAway() {
    std::printf("What happens when two ends do not agree on what a world is:\n");

    const ScopedEngineSchema wire;

    ResourceManager resources;
    Scene serverWorld;
    NetSession server;
    server.onSpawn([](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
                   [](Scene&, ResourceManager&, PlayerId, EntityId) {});
    check("the server is up", server.host(0, 4));

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    // The client says hello carrying a fingerprint of what it replicates, and
    // the datagram is on the wire before the list changes - so the server reads
    // what this build sent, which in the real world is simply another build.
    client.beginTick(1, InputCommand{}, 3);
    client.send(clientWorld, 1);

    NetSchema::get().replicate<Transform>("Health");

    bool refused = false;
    for (int i = 0; i < 20 && !refused; ++i) {
        server.receive(serverWorld, resources);
        server.send(serverWorld, 1);
        client.receive(clientWorld, resources);
        refused = client.isOffline() && !client.lastError().empty();
    }
    check("the client is refused rather than left to decode nonsense", refused);
    check("and told why", client.lastError() == std::string("a different build or world"));
    check("no seat was taken", server.playerCount() == 0);

    server.close();
}

void testAFullServerSaysSoRatherThanIgnoring() {
    std::printf("A server with one seat, and two players who want it:\n");

    const ScopedEngineSchema wire;

    ResourceManager resources;
    Scene serverWorld;
    NetSession server;
    server.onSpawn([](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
                   [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); });
    server.host(0, 1);
    const uint16_t port = server.localAddress().port;

    Scene firstWorld, secondWorld;
    NetSession first, second;
    first.connect(NetAddress{0x7F000001u, port});

    uint32_t tick = 0;
    const auto pump = [&]() {
        server.receive(serverWorld, resources);
        first.receive(firstWorld, resources);
        second.receive(secondWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        first.beginTick(tick, InputCommand{}, 3);
        second.beginTick(tick, InputCommand{}, 3);
        server.send(serverWorld, tick);
        first.send(firstWorld, tick);
        second.send(secondWorld, tick);
    };

    check("the first player gets the seat",
          pumpUntil(pump, [&]() { return first.isPlaying(); }));

    second.connect(NetAddress{0x7F000001u, port});
    bool told = false;
    for (int i = 0; i < 20 && !told; ++i) {
        pump();
        told = second.isOffline() && !second.lastError().empty();
    }
    check("the second is told the game is full", told);
    check("in words a player can be shown",
          second.lastError() == std::string("the server is full"));
    check("and the first player is undisturbed", first.isPlaying());

    first.close();
    server.close();
}

void testASnapshotNeverOutgrowsThePacketItRidesIn() {
    std::printf("The entry that is never deferred, and the budget it can still blow:\n");

    NetSchema schema;
    fillWorldSchema(schema);

    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 90);

    // Far up the slot range, past everything else. An id is a gap from the one
    // before it, and a gap this size costs three times a neighbouring one -
    // which is what matters, since the room is reserved before the gaps are known.
    const EntityId owner = server.createEntityAt(5000);
    Transform at;
    at.position = {40.0f, 4.0f, 0.0f};
    server.add(owner, at);
    server.add(owner, Rigidbody{});

    server.forEach<Rigidbody>([&](EntityId, Rigidbody& body) {
        body.linearVelocity  = {3.0f, -9.0f, 1.5f};
        body.angularVelocity = {0.0f, 2.0f, 0.0f};
    });

    // Every budget across the range where the world does not fit. The
    // connection's own entity is written wherever slot order puts it and is
    // never deferred, so on exactly the budgets where everything else has just
    // filled the packet, it is the one entry that can carry the body past the
    // end - and the datagram it rides in was sized from that same budget, so
    // the overflow has nowhere to go and the whole snapshot is lost. It arrives
    // as a header with nothing behind it, which reads as a packet that would
    // not decode: a symptom several steps from its cause.
    // The owner is last in slot order on purpose: everything else has spent the
    // budget by the time the write reaches it, which is the only arrangement
    // where the entry that cannot be deferred is also the one with no room.
    uint32_t over = 0;
    uint32_t worst = 0;
    uint32_t budgets = 0;
    for (uint32_t bytes = 40; bytes <= 1200; ++bytes) {
        NetBaseline baseline;
        NetBudget budget;
        budget.bytes = bytes;
        budget.owner = owner;
        ++budgets;

        std::vector<uint8_t> body;
        const NetSnapshotStats stats = writeSnapshot(server, schema, 1, budget, baseline, body);
        if (body.size() > bytes) {
            ++over;
            worst = std::max(worst, static_cast<uint32_t>(body.size()) - bytes);
        }
        check("the owner is in it whatever the budget", stats.entitiesWritten >= 1);
    }
    check("and no budget produces a body larger than itself", over == 0);
    std::printf("      %u budgets from 40 to 1200 bytes, %u over by up to %u byte(s)\n",
                budgets, over, worst);
}

void testAClientThatLosesItsServerDoesNotInheritTheWorld() {
    std::printf("What a client decides once the server has gone:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const std::vector<EntityId> crates = buildCrates(serverWorld, 8);

    NetSession server;
    server.onSpawn([](Scene& scene, ResourceManager&, PlayerId) {
                       const EntityId e = scene.createEntity();
                       scene.add(e, Transform{});
                       scene.add(e, Rigidbody{});
                       return e;
                   },
                   [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); });
    server.host(0, 4);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 0);
        client.beginTick(tick, InputCommand{}, 0);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and the world arrives",
          pumpUntil(frame, [&]() { return clientWorld.isAliveAtIndex(crates.back().slot()); }));

    const EntityId mine   = client.localEntity();
    const EntityId theirs = clientWorld.entityAt(crates.back().slot());
    check("it decides its own", client.simulates(mine));
    check("and not a crate", !client.simulates(theirs));

    // The server goes quiet without saying goodbye - the process is gone,
    // which is the ordinary way a game ends. Its socket stays open and
    // unattended, so nothing answers and nothing refuses.
    for (int i = 0; i < 400 && !client.isDisconnected(); ++i) {
        client.receive(clientWorld, resources);
        client.advance(1.0f / 60.0f);
    }
    check("the client notices", client.isDisconnected());

    // The half that matters. Offline means "decide everything, there is nobody
    // else"; this end decided nothing all session and has just lost the one who
    // did. Told it was Offline it would take a world it had only been shown.
    check("it does not become the authority", !client.simulates(theirs));
    check("nor over what it used to drive", !client.simulates(mine));
    check("and it owns nothing now", !client.isMine(mine));
    check("which is not the same as never having played", !client.isOffline());
    check("and it says why", client.lastError() == std::string("the server stopped answering"));
}

void testAStutterDoesNotCostTheTicksItRan() {
    std::printf("A frame that ran thirty-two ticks, and one packet to say so:\n");

    // The clock runs up to a quarter of a second of ticks in one frame, which
    // is thirty-two at 128 Hz, and a frame sends one packet. More commands than
    // a packet holds is therefore ordinary after any stall - a window the
    // compositor stopped drawing, a scene load, a hitch.
    std::vector<InputCommand> made;
    for (uint32_t i = 0; i < 32; ++i) {
        InputCommand c;
        c.sequence = i;
        c.tick     = 1000 + i;
        c.axis[0]  = 1.0f;
        if (i == 0) c.pressed = 1u << 2;   // the jump, on the oldest of them
        made.push_back(c);
    }

    NetCommandBuffer server;
    std::vector<uint8_t> packet;
    std::vector<InputCommand> got;
    std::vector<InputCommand> pending = made;

    // Packet after packet, each carrying what one frame could, with the server
    // acknowledging nothing. Everything the client ran has to reach it.
    int packets = 0;
    std::set<uint32_t> arrived;
    while (!pending.empty() && packets < 10) {
        BitWriter writer(packet, 1024);
        writeCommands(writer, pending, 6);
        writer.finish();

        BitReader reader(packet.data(), packet.size());
        check("the packet decodes", readCommands(reader, 6, got));
        for (const InputCommand& c : got) server.accept(c);

        // What went is gone; the rest waits for the next frame.
        for (const InputCommand& c : got) arrived.insert(c.sequence);
        pending.erase(pending.begin(),
                      pending.begin() + static_cast<long>(std::min(got.size(), pending.size())));
        ++packets;
    }

    check("one packet could not hold them", packets > 1);
    check("but every one of them reached the server, by sequence",
          arrived.size() == made.size() && *arrived.begin() == 0
          && *arrived.rbegin() == made.size() - 1);
    check("with none left behind on the client", pending.empty());

    // What the server does with a backlog is a separate and deliberate thing:
    // it drains faster than it fills, because a queue is input delay. Skipping
    // an axis is free - the one that runs says where the stick is now - and an
    // edge is not, so skipped edges are folded into the command that runs. The
    // jump was on the oldest command of the burst, which is exactly the one
    // sending the newest first would have stranded forever.
    int jumps = 0;
    for (uint32_t tick = 0; tick < 40; ++tick) {
        if (server.take(2000 + tick).pressed & (1u << 2)) ++jumps;
    }
    check("and the jump on the oldest of them survived, exactly once", jumps == 1);
    std::printf("      32 ticks in one frame, all delivered across %d packets\n", packets);
}

void testAServerThatHasBeenUpAWhileStillTakesAJoin() {
    std::printf("A server nobody restarted, and a client that has just started:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const std::vector<EntityId> crates = buildCrates(serverWorld, 6);

    NetSession server;
    server.onSpawn([](Scene& scene, ResourceManager&, PlayerId) {
                       const EntityId e = scene.createEntity();
                       scene.add(e, Transform{});
                       scene.add(e, Rigidbody{});
                       return e;
                   },
                   [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); });
    server.host(0, 4);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    // The server has been running for a while. Its tick is a count of its own
    // ticks since it started, and a client that has just started is at nearly
    // zero - the two clocks were never related and nothing tries to relate
    // them. Four minutes at 128 Hz is enough to be past the jump the client
    // refuses a header for.
    uint32_t serverTick = Config::MAX_TICK_RATE * 60 + 5000;
    uint32_t clientTick = 0;

    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++serverTick;
        ++clientTick;
        server.beginTick(serverTick, InputCommand{}, 0);
        client.beginTick(clientTick, InputCommand{}, 0);
        server.endTick(serverWorld, serverTick);
        client.endTick(clientWorld, clientTick);
        server.send(serverWorld, serverTick);
        client.send(clientWorld, clientTick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client is welcomed", pumpUntil(frame, [&]() { return client.isPlaying(); }, 80));

    // The world has to arrive. A header refused for claiming a tick this end
    // has not reached is a client that is connected, seated, and blind.
    const bool arrived = pumpUntil(frame,
        [&]() { return clientWorld.isAliveAtIndex(crates.back().slot()); }, 200);
    check("and the world it joined arrives", arrived);
    std::printf("      server at tick %u, client at tick %u, %zu entities across\n",
                serverTick, clientTick, clientWorld.entityCount());

    client.close();
    server.close();
}

void testStandingIsToldRatherThanGuessed() {
    std::printf("The five micrometres that had every other player mid-jump:\n");

    const ScopedEngineSchema wire;

    // A floor and a character resting on it, built identically on both ends -
    // a slot is an entity's name on the wire, and the two agree by having
    // loaded the same scene.
    const auto addWorld = [](Scene& scene, EntityId& floor) {
        floor = addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
        const EntityId walker = scene.createEntity();
        Transform at;
        at.position = {0.0f, 0.0f, 0.0f};
        scene.add(walker, at);

        Rigidbody body;
        body.mass           = 70.0f;
        body.freezeRotation = true;
        scene.add(walker, body);

        Collider capsule;
        ColliderPart part;
        part.shape      = ColliderShape::Capsule;
        part.radius     = 0.3f;
        part.halfHeight = 0.6f;
        part.center     = {0.0f, 0.9f, 0.0f};
        capsule.parts   = {part};
        scene.add(walker, capsule);

        scene.add(walker, CharacterController{});
        return walker;
    };

    Scene serverWorld;
    EntityId serverFloor;
    const EntityId walker = addWorld(serverWorld, serverFloor);

    NetSession server;
    server.onSpawn([&](Scene& scene, ResourceManager&, PlayerId) {
                       const EntityId spare = scene.createEntity();
                       scene.add(spare, Transform{});
                       scene.add(spare, Rigidbody{});
                       return spare;
                   },
                   [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); });
    server.host(0, 2);

    Scene clientWorld;
    EntityId clientFloor;
    const EntityId clientWalker = addWorld(clientWorld, clientFloor);
    (void)clientFloor;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    PhysicsSystem serverPhysics, clientPhysics;
    CharacterControllerSystem serverWalk, clientWalk;
    Clock clock;
    EventBus events;
    WindowManager window;
    InputMap input;
    ResourceManager resources;
    FrameContext serverCtx{serverWorld, resources, clock, events, window, input, server};
    FrameContext clientCtx{clientWorld, resources, clock, events, window, input, client};

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 0);
        client.beginTick(tick, InputCommand{}, 0);
        serverWalk.fixedUpdate(serverCtx);
        clientWalk.fixedUpdate(clientCtx);
        serverPhysics.fixedUpdate(serverCtx);
        clientPhysics.fixedUpdate(clientCtx);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    for (int i = 0; i < 30; ++i) frame();

    check("the server has the character standing",
          serverWorld.get<CharacterController>(walker).grounded);
    check("and the client does not decide this one",
          !client.simulates(clientWalker));

    // Leave the client nothing to measure. Whether a collider is on does not
    // replicate, so this sticks where moving the floor would not. In a real
    // scene the same gap opens from rounding, less neatly.
    clientWorld.get<Collider>(clientWalker).enabled = false;
    for (int i = 0; i < 30; ++i) frame();

    // Gravity renews the contact under a resting capsule every tick, and a
    // client applies none to a body it does not decide - so it can see the
    // character in the right place and find nothing under it.
    check("the client finds nothing under the character",
          !clientWorld.get<Rigidbody>(clientWalker).supported);
    check("and is told it is standing all the same",
          clientWorld.get<CharacterController>(clientWalker).grounded);

    client.close();
    server.close();
}

void testAClientDoesNotMoveWhatItDoesNotOwn() {
    std::printf("Why two clients see the same tower fall the same way:\n");

    const ScopedEngineSchema wire;

    const auto addCrate = [](Scene& scene, float y) {
        const EntityId crate = scene.createEntity();
        Transform at;
        at.position = {0.0f, y, 0.0f};
        scene.add(crate, at);
        Rigidbody body;
        body.mass = 2.0f;
        scene.add(crate, body);
        Collider box;
        ColliderPart part;
        part.shape       = ColliderShape::Box;
        part.halfExtents = {0.5f, 0.5f, 0.5f};
        box.parts        = {part};
        scene.add(crate, box);
        return crate;
    };

    Scene serverWorld;
    const EntityId crate = addCrate(serverWorld, 5.0f);

    NetSession server;
    server.onSpawn([&](Scene& scene, ResourceManager&, PlayerId) { return addCrate(scene, 20.0f); },
                   [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); });
    server.host(0, 2);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    PhysicsSystem serverPhysics, clientPhysics;
    Clock clock;
    EventBus events;
    WindowManager window;
    InputMap input;
    ResourceManager resources;
    FrameContext serverCtx{serverWorld, resources, clock, events, window, input, server};
    FrameContext clientCtx{clientWorld, resources, clock, events, window, input, client};

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 0);
        client.beginTick(tick, InputCommand{}, 0);
        serverPhysics.fixedUpdate(serverCtx);
        clientPhysics.fixedUpdate(clientCtx);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        // A frame is time passing. The wire is clocked by that rather than by
        // how often anybody calls send, so a test that never says time passed
        // gets one packet and then silence - correctly.
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and is a client, not an authority", client.role() == NetRole::Client);

    check("the crate reaches it",
          pumpUntil(frame, [&]() { return clientWorld.isAliveAtIndex(crate.slot()); }));
    const EntityId mirror = clientWorld.entityAt(crate.slot());
    check("and does not decide the crate's fate", !client.simulates(mirror));
    check("while it does decide its own", client.simulates(client.localEntity()));

    for (int i = 0; i < 30; ++i) frame();

    const float serverY = serverWorld.get<Transform>(crate).position.y;
    const float clientY = clientWorld.get<Transform>(mirror).position.y;
    check("the server's crate fell", serverY < 4.9f);

    // The client trails the server by about the time a packet takes, which is
    // what a client is: it draws a past the server has already left. What it
    // must not do is invent a present of its own.
    check("the client is behind the server, not ahead of it", clientY >= serverY);
    // Behind by about the gap between snapshots, which is what being a client
    // costs. At 32 snapshots a second a body accelerating under gravity moves a
    // few centimetres between them.
    check("behind by about the gap between snapshots, not by a guess",
          clientY - serverY < 0.3f);
    std::printf("      server %.3f m, client %.3f m, %.0f mm apart\n",
                serverY, clientY, (clientY - serverY) * 1000.0f);

    // The unambiguous half: with the server silent, a client simulating the
    // crate would keep dropping it - and two clients dropping it from different
    // moments is the divergence a player calls "we see different worlds".
    const float before = clientWorld.get<Transform>(mirror).position.y;
    for (int i = 0; i < 60; ++i) {
        ++tick;
        client.beginTick(tick, InputCommand{}, 0);
        clientPhysics.fixedUpdate(clientCtx);
    }
    check("with nothing arriving, the client does not move it a hair",
          std::abs(clientWorld.get<Transform>(mirror).position.y - before) < 1e-6f);

    client.close();
    server.close();
}

void testTheWorldIsDrawnSmoothlyBetweenWhatArrives() {
    std::printf("Sixty snapshots a second, drawn at a hundred and forty-four:\n");

    NetInterpolation smoothing;
    Scene scene;
    const EntityId crate = scene.createEntity();
    Transform at;
    scene.add(crate, at);

    // A body sliding along x at one metre a tick, described once every four
    // ticks - a snapshot rate below the tick rate, which is the normal case -
    // while frames run at 144 a second against a 60 Hz simulation.
    const float tickRate  = 60.0f;
    const float frameTime = 1.0f / 144.0f;

    float simulated = 0.0f;   ///< Where the server's clock has got to.
    uint32_t nextSnapshot = 0;
    float previous = -1.0f;
    int forward = 0, frames = 0, stepped = 0;

    for (int i = 0; i < 300; ++i) {
        simulated += frameTime * tickRate;
        while (static_cast<float>(nextSnapshot) <= simulated) {
            smoothing.record(crate, nextSnapshot,
                             {static_cast<float>(nextSnapshot), 0.0f, 0.0f},
                             glm::angleAxis(glm::radians(static_cast<float>(nextSnapshot)),
                                            Math::WORLD_UP));
            nextSnapshot += 4;
        }

        smoothing.apply(scene, frameTime, tickRate, 32.0f);
        const float x = scene.get<Transform>(crate).position.x;
        if (i > 20) {
            if (x > previous + 1e-6f) ++forward;
            else                      ++stepped;
            ++frames;
        }
        previous = x;
    }
    check("it is tracking the body", smoothing.tracked() == 1);
    check("every frame moved it, not one frame in four", forward > frames * 9 / 10);

    // And it draws the past on purpose. Drawing the newest sample is what makes
    // a body step; drawing a moment with data on both sides of it is what makes
    // it slide.
    const float newest = static_cast<float>(nextSnapshot - 4);
    check("it is drawing behind the newest thing it heard",
          smoothing.renderTick() < newest);
    check("but not far behind it", newest - smoothing.renderTick() < 12.0f);
    std::printf("      newest news tick %.0f, drawing tick %.1f, %d of %d frames moved\n",
                newest, smoothing.renderTick(), forward, frames);
    (void)stepped;

    // With nothing more arriving it holds rather than guesses. A body that
    // overshoots and is pulled back reads worse than one that pauses for the
    // length of a lost packet.
    for (int i = 0; i < 400; ++i) smoothing.apply(scene, frameTime, tickRate, 32.0f);
    check("and never runs past the last thing it was told",
          scene.get<Transform>(crate).position.x <= newest + 1e-4f);

    // A sample older than one already held is dropped: the newer one already
    // describes a moment past it, and inserting it would put the history out of
    // the order the search relies on.
    smoothing.record(crate, 8, {-500.0f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0));
    smoothing.apply(scene, frameTime, tickRate, 32.0f);
    check("a sample that arrived late and stale is ignored",
          scene.get<Transform>(crate).position.x > 0.0f);
}

void testSmoothingNeverFeedsItselfItsOwnGuess() {
    std::printf("The error that compounds until a still body drifts away:\n");

    // Presence in a snapshot is measured against what the receiver was last
    // told. So if the smoothed value - which is deliberately a moment behind -
    // is sitting in the component when the next snapshot lands, "unchanged"
    // silently means "unchanged from the smoothed value", and the body walks.
    NetInterpolation smoothing;
    Scene scene;
    const EntityId crate = scene.createEntity();
    Transform at;
    at.position = {10.0f, 0.0f, 0.0f};
    scene.add(crate, at);

    smoothing.record(crate, 10, {10.0f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0));
    smoothing.record(crate, 20, {20.0f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0));

    // Draw a few frames: the component now holds something between the two.
    for (int i = 0; i < 5; ++i) smoothing.apply(scene, 1.0f / 60.0f, 60.0f, 32.0f);
    const float drawn = scene.get<Transform>(crate).position.x;
    check("the drawn position is behind the newest", drawn < 20.0f);

    // A snapshot arrives saying nothing about this body, so the reader leaves
    // the component alone. Restoring first is what makes "leaves it alone" mean
    // what the server said rather than what was drawn.
    smoothing.restoreConfirmed(scene);
    check("the server's last word is put back before a snapshot is read",
          std::abs(scene.get<Transform>(crate).position.x - 20.0f) < 1e-6f);
}

void testAPlayerIsNotDraggedBackwardsByTheirOwnConnection() {
    std::printf("The one thing a player always notices:\n");

    const ScopedEngineSchema wire;

    ResourceManager resources;

    Scene serverWorld;
    const EntityId walker = addWalker(serverWorld);

    NetSession server;
    server.onSpawn([walker](Scene&, ResourceManager&, PlayerId) { return walker; },
                   [](Scene&, ResourceManager&, PlayerId, EntityId) {});
    server.host(0, 2);

    Scene clientWorld;
    const EntityId clientWalker = addWalker(clientWorld);
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    // One rule, run by both ends: step along x by whatever the command says.
    // That is what prediction is - the client running the rule the server will
    // run, on its own input, without waiting to be told the answer.
    const auto step = [](Scene& scene, NetSession& net, EntityId entity) {
        if (!net.isPlaying() || !scene.isAlive(entity)) return;
        scene.get<Transform>(entity).position.x += net.commandFor(entity).axis[0];
    };

    uint32_t tick = 0;
    int replayed = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);

        // What Engine::run does between receiving and this frame's own ticks:
        // every tick the server disagreed about is run again from the server's
        // answer, under the command it originally ran.
        for (const InputCommand& again : client.replayCommands()) {
            client.beginReplayTick(again);
            step(clientWorld, client, clientWalker);
            client.endTick(clientWorld, again.tick);
            ++replayed;
        }
        client.endReplay(clientWorld);

        ++tick;

        InputCommand walking;
        walking.sequence = tick;
        walking.tick     = tick;
        walking.axis[0]  = 1.0f;      // held forward, every tick
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, walking, 3);

        step(serverWorld, server, walker);
        step(clientWorld, client, clientWalker);

        client.endTick(clientWorld, tick);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(0.016f);
        client.advance(0.016f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and drives the character", client.localEntity().slot() == walker.slot());

    // Let the two settle into step first. The very first snapshot after a join
    // is compared against a prediction made before the server had any of this
    // client's input, so one correction there is the mechanism working.
    for (int i = 0; i < 20; ++i) frame();
    replayed = 0;
    for (int i = 0; i < 60; ++i) frame();

    const float clientX = clientWorld.get<Transform>(clientWalker).position.x;
    const float serverX = serverWorld.get<Transform>(walker).position.x;

    // The server is behind by the commands still in flight and always will be.
    // A client that believed each snapshot would jump its character back by
    // that much on every packet, which reads as broken rather than as lag.
    check("the server is describing a moment the client has passed", serverX < clientX);
    check("and the client's own character is still out in front",
          clientX > serverX + 0.5f);

    // And the two ran the same rule on the same input, so once the server
    // catches up to a tick the client predicted, they agree exactly. That is
    // the number that decides whether the character feels solid.
    check("their answers for the same tick agree", client.predictionError() < 0.01f);
    check("so once they are in step, nothing has to be run again at all",
          replayed == 0);
    std::printf("      client at %.1f, server at %.1f, prediction out by %.4f m\n",
                clientX, serverX, static_cast<double>(client.predictionError()));

    client.close();
    server.close();
}

void testAPredictionThatWasWrongIsRunAgainRatherThanArguedWith() {
    std::printf("And what happens when the client really was wrong:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const EntityId walker = addWalker(serverWorld);
    NetSession server;
    server.onSpawn([walker](Scene&, ResourceManager&, PlayerId) { return walker; },
                   [](Scene&, ResourceManager&, PlayerId, EntityId) {});
    server.host(0, 2);

    Scene clientWorld;
    const EntityId clientWalker = addWalker(clientWorld);
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    // The client believes it may walk. The server holds it against a wall the
    // client does not know about, so every tick the client predicts is wrong by
    // one more step - which is exactly the case a nudge closes slowly and a
    // replay closes at once.
    bool pushing = false;
    const auto stepClient = [&](const InputCommand& command) {
        if (!clientWorld.isAlive(clientWalker)) return;
        clientWorld.get<Transform>(clientWalker).position.x += command.axis[0] * 0.1f;
    };

    uint32_t tick = 0;
    int replayed = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);

        for (const InputCommand& again : client.replayCommands()) {
            client.beginReplayTick(again);
            stepClient(again);
            client.endTick(clientWorld, again.tick);
            ++replayed;
        }
        client.endReplay(clientWorld);

        ++tick;
        InputCommand walking;
        walking.sequence = tick;
        walking.tick     = tick;
        walking.axis[0]  = pushing ? 1.0f : 0.0f;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, walking, 3);

        // The wall: whatever the client sends, the server does not move it.
        if (serverWorld.isAlive(walker)) serverWorld.get<Transform>(walker).position.x = 0.0f;
        stepClient(client.commandFor(clientWalker));

        client.endTick(clientWorld, tick);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));

    pushing = true;
    for (int i = 0; i < 15; ++i) frame();
    const float wandered = clientWorld.get<Transform>(clientWalker).position.x;
    check("the client walked into a wall it could not see", wandered > 0.05f);
    check("was told its answer was wrong", client.predictionError() > 0.0f);
    check("and re-ran the ticks rather than arguing with the answer", replayed > 0);

    // It stops pushing. Every tick since the disagreement was recomputed from
    // the server's own state, so the next snapshot to arrive lands it exactly -
    // not a fifth of the way, and not over the following second.
    pushing = false;
    const int before = replayed;
    for (int i = 0; i < 6; ++i) frame();

    const float settled = clientWorld.get<Transform>(clientWalker).position.x;
    check("and lands on the server's answer, not near it",
          std::abs(settled) < 0.001f);
    std::printf("      wandered %.3f m, %d tick(s) re-run, settled at %.5f\n",
                static_cast<double>(wandered), replayed, static_cast<double>(settled));
    (void)before;

    client.close();
    server.close();
}

void testAWorldConvergesThroughALossyLink() {
    std::printf("A third of every packet lost, reordered and duplicated:\n");

    NetSchema schema;
    fillWorldSchema(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 60);

    NetBaseline baseline;
    NetBudget budget;
    budget.owner = crates.front();

    Scene client;

    // Seeded rather than random, so a failure is a failure every run and not a
    // story about a seed. The point is not which packets are lost but that the
    // design does not care which.
    Math::Rng noise(12345u);
    const auto roll = [&noise](int upTo) { return noise.nextInt(0, upTo - 1); };

    // Snapshots written but not yet delivered, so they can arrive late, twice,
    // or never - which is what a network does and what the acknowledgement
    // design has to survive.
    struct InFlight {
        uint16_t             sequence = 0;
        std::vector<uint8_t> body;
    };
    std::vector<InFlight> wire;

    uint16_t sequence = 1;
    int sent = 0, lost = 0, delivered = 0, duplicated = 0;

    for (int round = 0; round < 400; ++round) {
        // The world keeps moving for the first half, then settles - which is
        // when a frozen body would show up, because nothing later would correct
        // it.
        if (round < 200) {
            for (size_t i = 0; i < crates.size(); ++i) {
                if (roll(3) != 0) continue;
                server.get<Transform>(crates[i]).position +=
                    glm::vec3(0.05f, 0.0f, 0.02f);
            }
        }

        std::vector<uint8_t> body;
        writeSnapshot(server, schema, sequence, budget, baseline, body);
        wire.push_back({sequence, body});
        ++sequence;
        ++sent;

        // Delivery, out of order and unreliable.
        for (size_t i = wire.size(); i-- > 0;) {
            const int die = roll(100);
            if (die < 33) {                       // lost outright
                wire.erase(wire.begin() + static_cast<long>(i));
                ++lost;
                continue;
            }
            if (die < 50 && wire.size() < 8) continue;   // held, arrives later

            readSnapshotFrom(client, schema, wire[i].body);
            baseline.confirm(wire[i].sequence);
            ++delivered;

            if (die > 95) {                       // and again, as a duplicate
                readSnapshotFrom(client, schema, wire[i].body);
                baseline.confirm(wire[i].sequence);
                ++duplicated;
            }
            wire.erase(wire.begin() + static_cast<long>(i));
        }
    }

    // Drain whatever is still in flight, then let it settle.
    for (int round = 0; round < 40; ++round) {
        std::vector<uint8_t> body;
        writeSnapshot(server, schema, sequence, budget, baseline, body);
        readSnapshotFrom(client, schema, body);
        baseline.confirm(sequence);
        ++sequence;
    }

    check("packets really were lost", lost > sent / 5);
    check("and some arrived twice", duplicated > 0);

    // The property that matters: after all that, the two worlds are the same.
    // Not close - the same, to the millimetre the wire carries.
    float worst = 0.0f;
    int missing = 0;
    for (EntityId crate : crates) {
        if (!client.isAliveAtIndex(crate.slot())) { ++missing; continue; }
        const glm::vec3 there = client.get<Transform>(client.entityAt(crate.slot())).position;
        const glm::vec3 here  = server.get<Transform>(crate).position;
        worst = std::max(worst, glm::length(there - here));
    }
    check("every body is on the client", missing == 0);
    check("and every one of them is where the server has it", worst < 0.002f);
    std::printf("      %d sent, %d lost, %d delivered, %d duplicated; worst body off by %.4f mm\n",
                sent, lost, delivered, duplicated, static_cast<double>(worst) * 1000.0);

    // And it did not do it by shouting: a settled world under loss still goes
    // quiet, because presence is measured against what was confirmed.
    std::vector<uint8_t> body;
    const NetSnapshotStats quiet = writeSnapshot(server, schema, sequence, budget, baseline, body);
    check("a settled world is quiet even after all that",
          quiet.entitiesWritten <= 1);
}

void testAPacketCannotTalkTheDecoderPastTheEndOfACommand() {
    std::printf("What a peer claiming more actions than exist is told:\n");

    // A command holds a fixed number of action slots. The count is a byte on
    // the wire, so a peer can claim 255 of them - by being a different build,
    // by being corrupted in flight, or on purpose. The decoder indexes the
    // array with it, so believing it writes off the end of the command.
    std::vector<InputCommand> window;
    window.push_back(walkCommand(1, 100, 1.0f, true));

    std::vector<uint8_t> packet;
    BitWriter writer(packet, 512);
    writeCommands(writer, window, MAX_INPUT_ACTIONS + 1);
    writer.finish();
    check("the encoder refuses more actions than a command holds",
          writer.bitCount() == 0);

    // And the decoder refuses independently, because it is the side that would
    // do the writing and the only one that knows what the array can hold.
    BitWriter honest(packet, 512);
    writeCommands(honest, window, MAX_INPUT_ACTIONS);
    honest.finish();

    std::vector<InputCommand> got;
    BitReader hostile(packet.data(), packet.size());
    check("and the decoder refuses to read that many",
          !readCommands(hostile, MAX_INPUT_ACTIONS + 1, got));
    check("without having written anything first", got.empty());

    // The legal maximum still works, so the refusal is a bound and not an
    // off-by-one that quietly costs a project its last action.
    BitReader legal(packet.data(), packet.size());
    check("while every slot a command really has is still usable",
          readCommands(legal, MAX_INPUT_ACTIONS, got) && got.size() == 1);
}

void testAMessageThatMustArriveDoes() {
    std::printf("The few things no later packet would say again:\n");

    NetReliable sender, receiver;
    const auto message = [](const char* text) {
        return std::vector<uint8_t>(text, text + std::strlen(text));
    };

    const std::vector<uint8_t> one   = message("spawn:crate");
    const std::vector<uint8_t> two   = message("spawn:barrel");
    const std::vector<uint8_t> three = message("despawn:crate");
    check("a message is accepted", sender.queue(one.data(), one.size()));
    check("and another", sender.queue(two.data(), two.size()));
    check("and a third", sender.queue(three.data(), three.size()));
    check("all three are waiting", sender.pending() == 3);

    // Every packet the sender writes carries the oldest unconfirmed messages.
    // Losing packets costs nothing but time.
    std::vector<uint8_t> packet;
    std::vector<std::vector<uint8_t>> got;
    const auto exchange = [&](bool deliver, bool reply) {
        packet.clear();
        BitWriter writer(packet, 1024);
        sender.write(writer);
        writer.finish();
        if (!deliver) return;

        BitReader reader(packet.data(), packet.size());
        check("the block decodes", receiver.read(reader, got));

        if (!reply) return;
        std::vector<uint8_t> back;
        BitWriter backWriter(back, 1024);
        receiver.write(backWriter);
        backWriter.finish();
        std::vector<std::vector<uint8_t>> none;
        BitReader backReader(back.data(), back.size());
        sender.read(backReader, none);
        check("nothing comes back the other way", none.empty());
    };

    // Ten packets lost outright before one gets through.
    for (int i = 0; i < 10; ++i) exchange(false, false);
    check("nothing was delivered while every packet was lost", got.empty());
    check("and the sender still holds all of them", sender.pending() == 3);

    exchange(true, false);
    check("one packet arriving delivers all three", got.size() == 3);
    check("in the order they were made",
          got[0] == one && got[1] == two && got[2] == three);

    // The sender has not heard yet, so it keeps sending them - and the receiver
    // must not deliver them a second time.
    exchange(true, false);
    check("a repeat is not delivered twice", got.size() == 3);
    check("and the sender is still holding them", sender.pending() == 3);

    // Now the acknowledgement gets back.
    exchange(true, true);
    check("once confirmed, the sender lets them go", sender.pending() == 0);
    check("and still delivered each exactly once", got.size() == 3);

    // A message queued afterwards flows on the same channel.
    const std::vector<uint8_t> four = message("spawn:ramp");
    sender.queue(four.data(), four.size());
    exchange(true, true);
    check("a later message arrives too", got.size() == 4 && got[3] == four);
    check("and is confirmed", sender.pending() == 0);
}

void testAMessageBlockRefusesWhatItCannotCarry() {
    std::printf("What the channel will not do:\n");

    NetReliable channel;
    const std::vector<uint8_t> huge(NetReliable::MAX_MESSAGE + 1, 0x7Fu);
    check("a message too big for a packet is refused, not truncated",
          !channel.queue(huge.data(), huge.size()));
    check("and an empty one is refused too", !channel.queue(huge.data(), 0));
    check("neither was queued", channel.pending() == 0);

    // A peer that never confirms must not grow the queue without limit.
    const std::vector<uint8_t> small(8, 1u);
    size_t accepted = 0;
    for (size_t i = 0; i < NetReliable::MAX_QUEUED + 8; ++i) {
        if (channel.queue(small.data(), small.size())) ++accepted;
    }
    check("a peer that stops confirming fills the queue and no more",
          accepted == NetReliable::MAX_QUEUED);
    check("which is a connection to give up on, and says so",
          channel.pending() == NetReliable::MAX_QUEUED);

    // More than one packet's worth: the block carries what fits and the rest
    // waits, still in order.
    NetReliable big, far;
    const std::vector<uint8_t> chunk(NetReliable::MAX_MESSAGE, 0x2Au);
    for (int i = 0; i < 6; ++i) big.queue(chunk.data(), chunk.size());

    std::vector<uint8_t> packet;
    BitWriter writer(packet, 4096);
    big.write(writer);
    writer.finish();
    check("one packet's block stays inside its reservation",
          packet.size() <= NetReliable::BUDGET_BYTES + 32);

    std::vector<std::vector<uint8_t>> got;
    BitReader reader(packet.data(), packet.size());
    check("and what it carried decodes", far.read(reader, got));
    check("carrying fewer than were queued", got.size() < 6 && !got.empty());
    std::printf("      %zu of 6 messages of %zu bytes fit one packet\n",
                got.size(), NetReliable::MAX_MESSAGE);
}

void testSomethingBuiltAfterTheSceneLoadedReachesEveryone() {
    std::printf("A thing that did not exist when either end opened the world:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    // A prefab with a root and two children, written from a live subtree the
    // way the editor writes one.
    const char* const scratch = std::getenv("VKM_TEST_SCRATCH");
    const std::string prefabPath =
        (scratch ? std::filesystem::path(scratch) : std::filesystem::temp_directory_path())
        .append("net_spawn_test.prefab").string();
    {
        Scene authoring;
        const EntityId root = authoring.createEntity();
        Transform at;
        at.position = {1.0f, 2.0f, 3.0f};
        authoring.add(root, at);
        authoring.add(root, Rigidbody{});
        authoring.add(root, makeName("SpawnedThing"));
        for (int i = 0; i < 2; ++i) {
            const EntityId child = authoring.createEntity();
            Transform local;
            local.position = {static_cast<float>(i), 0.5f, 0.0f};
            authoring.add(child, local);
            authoring.add(child, makeName("Part"));
            HierarchyOperations::setParent(authoring, child, root);
        }
        check("a prefab is written from a live subtree",
              Prefab::save(authoring, root, prefabPath, resources));
    }

    Scene serverWorld;
    buildCrates(serverWorld, 5);
    NetSession server;
    server.onSpawn([](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
                   [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); });
    server.host(0, 4);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, InputCommand{}, 3);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and the authored world arrives",
          pumpUntil(frame, [&]() { return clientWorld.entityCount() >= 6; }, 80));

    // Now build something neither end had. This is the case the whole feature
    // exists for: a client told only where an entity is, having never been told
    // what it is, would draw nothing at all.
    Transform where;
    where.position = {-12.5f, 4.25f, 7.0f};
    const EntityId spawned = server.spawn(serverWorld, resources, prefabPath, where);
    check("the server builds it", serverWorld.isAlive(spawned));
    check("as a subtree, not one entity",
          childrenOf(serverWorld, spawned) == 2);

    check("and it reaches the client",
          pumpUntil(frame, [&]() { return clientWorld.isAliveAtIndex(spawned.slot()); }, 80));

    const EntityId mirror = clientWorld.entityAt(spawned.slot());
    check("at the same slot, which is its name on the wire", clientWorld.isAlive(mirror));
    check("the subtree built from the same file, not sent over the wire",
          childrenOf(clientWorld, mirror) == 2);
    check("marked as an instance, so a later save writes the reference",
          clientWorld.has<PrefabInstance>(mirror));
    check("placed where the server placed it",
          glm::length(clientWorld.get<Transform>(mirror).position - where.position) < 0.002f);

    // The root replicates; the children do not, because each end chose its own
    // slots for them. Moving the root must still move everything.
    serverWorld.get<Transform>(spawned).position = {40.0f, 1.0f, -2.0f};
    check("and moving it afterwards reaches the client too",
          pumpUntil(frame, [&]() {
              return std::abs(clientWorld.get<Transform>(mirror).position.x - 40.0f) < 0.002f;
          }, 80));

    // Despawning takes the subtree with it.
    const size_t before = clientWorld.entityCount();
    server.despawn(serverWorld, spawned);
    check("the server drops it", !serverWorld.isAliveAtIndex(spawned.slot()));
    check("and so does the client",
          pumpUntil(frame, [&]() { return !clientWorld.isAliveAtIndex(spawned.slot()); }, 80));
    check("taking its children with it, not leaking them",
          clientWorld.entityCount() == before - 3);

    std::remove(prefabPath.c_str());
    client.close();
    server.close();
}

void testAClientPredictsWhatItIsPushing() {
    std::printf("What a client is allowed to move because it is leaning on it:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    const auto addBox = [](Scene& scene, glm::vec3 at, glm::vec3 half, bool isStatic) {
        const EntityId body = scene.createEntity();
        Transform where;
        where.position = at;
        scene.add(body, where);
        Rigidbody rb;
        rb.mass     = 1.0f;
        rb.isStatic = isStatic;
        scene.add(body, rb);
        Collider box;
        ColliderPart part;
        part.shape       = ColliderShape::Box;
        part.halfExtents = half;
        box.parts        = {part};
        scene.add(body, box);
        return body;
    };

    // A floor, a crate resting on it, and a crate far away. The player is a
    // body dropped onto the near crate.
    EntityId floorId, nearCrate, farCrate, player;
    const auto build = [&](Scene& scene) {
        floorId   = addBox(scene, {0.0f, -0.5f, 0.0f}, {40.0f, 0.5f, 40.0f}, true);
        nearCrate = addBox(scene, {0.0f,  0.5f, 0.0f}, {0.5f, 0.5f, 0.5f}, false);
        farCrate  = addBox(scene, {20.0f, 0.5f, 0.0f}, {0.5f, 0.5f, 0.5f}, false);
        player    = addBox(scene, {0.0f,  1.6f, 0.0f}, {0.4f, 0.4f, 0.4f}, false);
        return player;
    };

    Scene serverWorld;
    build(serverWorld);
    const EntityId serverPlayer = player;
    const EntityId serverNear   = nearCrate;
    const EntityId serverFar    = farCrate;
    const EntityId serverFloor  = floorId;

    NetSession server;
    server.onSpawn([serverPlayer](Scene&, ResourceManager&, PlayerId) { return serverPlayer; },
                   [](Scene&, ResourceManager&, PlayerId, EntityId) {});
    server.host(0, 2);

    Scene clientWorld;
    build(clientWorld);
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    PhysicsSystem serverPhysics, clientPhysics;
    Clock clock;
    EventBus events;
    WindowManager window;
    InputMap input;
    FrameContext serverCtx{serverWorld, resources, clock, events, window, input, server};
    FrameContext clientCtx{clientWorld, resources, clock, events, window, input, client};

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, InputCommand{}, 3);
        serverPhysics.fixedUpdate(serverCtx);
        clientPhysics.fixedUpdate(clientCtx);
        client.endTick(clientWorld, tick);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and drives the player", client.localEntity().slot() == serverPlayer.slot());

    // Let it settle onto the crate.
    check("and comes to rest on the crate it was dropped on",
          pumpUntil(frame, [&]() { return client.leaseCount() > 0; }, 200));

    const EntityId clientNear  = clientWorld.entityAt(serverNear.slot());
    const EntityId clientFar   = clientWorld.entityAt(serverFar.slot());
    const EntityId clientFloor = clientWorld.entityAt(serverFloor.slot());

    check("the crate it is standing on is now its to move", client.simulates(clientNear));
    check("but it is still not the client's own entity", !client.isMine(clientNear));
    check("the floor is not: the closure stops at what cannot move",
          !client.simulates(clientFloor));
    check("and neither is a crate on the other side of the level",
          !client.simulates(clientFar));
    std::printf("      leasing %zu body(s) of %zu in the world\n",
                client.leaseCount(), clientWorld.entityCount());

    // Take the contact away. The lease must lapse - a crate touched once is not
    // the client's for the rest of the match.
    clientWorld.get<Transform>(client.localEntity()).position = {0.0f, 30.0f, 0.0f};
    serverWorld.get<Transform>(serverPlayer).position         = {0.0f, 30.0f, 0.0f};
    bool lapsed = false;
    for (int i = 0; i < 200 && !lapsed; ++i) {
        frame();
        lapsed = !client.simulates(clientNear);
    }
    check("and once nothing is touching it, the lease lapses", lapsed);

    client.close();
    server.close();
}

void testAShotIsJudgedAgainstWhatTheShooterCouldSee() {
    std::printf("Aiming at where somebody was, which is all anyone can do:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    // Two players: one that shoots and one that runs. Only players are rewound,
    // so a target has to be one - a crate would be judged against the present.
    Scene serverWorld;
    std::vector<EntityId> seats;
    const auto addPlayer = [&](Scene& scene) {
        const EntityId body = scene.createEntity();
        scene.add(body, Transform{});
        Rigidbody rb;
        rb.isKinematic = true;
        scene.add(body, rb);
        Collider box;
        ColliderPart part;
        part.shape       = ColliderShape::Box;
        part.halfExtents = {0.5f, 0.5f, 0.5f};
        box.parts        = {part};
        scene.add(body, box);
        return body;
    };
    seats.push_back(addPlayer(serverWorld));   // the shooter
    seats.push_back(addPlayer(serverWorld));   // the runner
    serverWorld.get<Transform>(seats[0]).position = {0.0f, 0.0f, -30.0f};

    size_t handedOut = 0;
    NetSession server;
    server.onSpawn([&](Scene&, ResourceManager&, PlayerId) {
                       return handedOut < seats.size() ? seats[handedOut++] : EntityId{};
                   },
                   [](Scene&, ResourceManager&, PlayerId, EntityId) {});
    server.host(0, 4);
    const uint16_t port = server.localAddress().port;

    Scene shooterWorld, runnerWorld;
    NetSession shooterClient, runnerClient;
    shooterClient.connect(NetAddress{0x7F000001u, port});
    runnerClient.connect(NetAddress{0x7F000001u, port});

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        shooterClient.receive(shooterWorld, resources);
        runnerClient.receive(runnerWorld, resources);

        // What Engine::run does every frame, and what advances the clock that
        // decides which moment a client is looking at - which is the number the
        // whole feature is built on.
        shooterClient.interpolate(shooterWorld, 1.0f / 60.0f, 60.0f);
        runnerClient.interpolate(runnerWorld, 1.0f / 60.0f, 60.0f);

        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        shooterClient.beginTick(tick, InputCommand{}, 3);
        runnerClient.beginTick(tick, InputCommand{}, 3);

        // The runner moves a metre a tick, so where it was N ticks ago is a
        // number rather than an estimate.
        if (handedOut > 1) {
            serverWorld.get<Transform>(seats[1]).position.x = static_cast<float>(tick);
        }

        server.endTick(serverWorld, tick);
        shooterClient.endTick(shooterWorld, tick);
        runnerClient.endTick(runnerWorld, tick);
        server.send(serverWorld, tick);
        shooterClient.send(shooterWorld, tick);
        runnerClient.send(runnerWorld, tick);
        server.advance(1.0f / 60.0f);
        shooterClient.advance(1.0f / 60.0f);
        runnerClient.advance(1.0f / 60.0f);
    };

    check("both players join",
          pumpUntil(frame, [&]() { return shooterClient.isPlaying() && runnerClient.isPlaying(); }, 80));
    const EntityId shooter = seats[0];
    check("and were given different characters",
          shooterClient.localEntity().slot() != runnerClient.localEntity().slot());

    for (int i = 0; i < 60; ++i) frame();

    const float now = serverWorld.get<Transform>(seats[1]).position.x;
    check("the runner has covered ground", now > 30.0f);

    const auto hits = [&](float x, bool rewound) {
        RayHit hit;
        QueryFilter filter;
        filter.ignore = shooter;
        if (!rewound) {
            return raycast(serverWorld, {x, 0.0f, -10.0f}, {0.0f, 0.0f, 1.0f}, 20.0f, hit, filter);
        }
        NetRewindScope scope(serverWorld, server, shooter);
        return raycast(serverWorld, {x, 0.0f, -10.0f}, {0.0f, 0.0f, 1.0f}, 20.0f, hit, filter);
    };

    check("a shot at where the runner is right now hits, judged live",
          hits(now, false));

    // The same shot judged against the present MISSES, because the runner was
    // not there when the player who fired could see anything. That is the
    // feature: aiming at the present is aiming where nobody can see.
    check("  and misses once the world is put back to the shooter's view",
          !hits(now, true));

    // The point of the whole feature: the shooter's screen is showing it a
    // moment already past, so a shot aimed there must be judged against that
    // moment rather than against the present.
    // Where the server puts the runner when it judges this player's shot -
    // measured rather than guessed at, so the test says something exact.
    float seenAt = 0.0f;
    {
        NetRewindScope scope(serverWorld, server, shooter);
        seenAt = serverWorld.get<Transform>(seats[1]).position.x;
    }
    check("the server puts the runner back to an earlier moment", seenAt < now - 0.5f);
    check("a shot aimed there would have missed the present", !hits(seenAt, false));
    check("  and lands when judged against what the shooter could see",
          hits(seenAt, true));
    std::printf("      runner now at %.1f, shooter was seeing %.1f - %.1f ticks back\n",
                static_cast<double>(now), static_cast<double>(seenAt),
                static_cast<double>(now - seenAt));

    // And nothing outside the scope can tell that the world moved.
    const glm::vec3 before = serverWorld.get<Transform>(seats[1]).position;
    { NetRewindScope scope(serverWorld, server, shooter); }
    check("the present is put back exactly",
          serverWorld.get<Transform>(seats[1]).position == before);

    shooterClient.close();
    runnerClient.close();
    server.close();
}

void testTwoSocketsCanTalk() {
    std::printf("Two UDP sockets on one machine:\n");

    UdpSocket server, client;
    check("a socket takes a port the system picks", server.open(0));
    check("  and reports which one it got", server.localAddress().port != 0);
    check("a second one opens beside it", client.open(0));
    check("  on a different port", client.localAddress().port != server.localAddress().port);

    std::vector<uint8_t> heard;
    NetAddress from;
    check("an empty socket says so rather than waiting", !server.receive(heard, from));
    check("  and leaves nothing behind to be read as a packet", heard.empty());

    const uint8_t hello[] = {'h', 'i', 0, 200, 255};
    check("a datagram goes out", client.send(server.localAddress(), hello, sizeof(hello)));

    // Loopback is not instant and is not ordered by the clock, so this waits
    // rather than assuming the next read has it.
    bool arrived = false;
    for (int i = 0; i < 200 && !arrived; ++i) {
        arrived = server.receive(heard, from);
        if (!arrived) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    check("  and arrives", arrived);
    check("  whole, bytes and all",
          heard.size() == sizeof(hello) && std::memcmp(heard.data(), hello, sizeof(hello)) == 0);
    check("  saying which port it came from", from.port == client.localAddress().port);

    // The reply proves the sender's address is usable, not merely reported.
    const uint8_t back[] = {'o', 'k'};
    check("and the answer goes back the way it came", server.send(from, back, sizeof(back)));

    std::vector<uint8_t> echo;
    NetAddress replyFrom;
    bool returned = false;
    for (int i = 0; i < 200 && !returned; ++i) {
        returned = client.receive(echo, replyFrom);
        if (!returned) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check("  and lands", returned && echo.size() == 2 && echo[0] == 'o');

    // What must never be sent: past this it is fragmented, and one lost
    // fragment loses the whole thing.
    const std::vector<uint8_t> huge(UdpSocket::MAX_DATAGRAM + 1, 0xAB);
    check("a datagram too big to survive the path is refused",
          !client.send(server.localAddress(), huge.data(), huge.size()));

    const std::vector<uint8_t> biggest(UdpSocket::MAX_DATAGRAM, 0xCD);
    check("  and the largest that fits is taken",
          client.send(server.localAddress(), biggest.data(), biggest.size()));
}

void testAnEmptyDatagramDoesNotEndTheDrain() {
    std::printf("A datagram of no bytes, behind which a real one is waiting:\n");

    UdpSocket server, client;
    check("two sockets open", server.open(0) && client.open(0));

    // Anyone can send one of these, and a host reads its socket until it says
    // empty. If nothing separates "a packet of no bytes" from "no packet", the
    // first one sent costs a host the rest of that frame's traffic.
    sockaddr_in to{};
    to.sin_family      = AF_INET;
    to.sin_addr.s_addr = htonl(0x7F000001u);
    to.sin_port        = htons(server.localAddress().port);

    const auto raw = ::socket(AF_INET, SOCK_DGRAM, 0);
    check("a raw sender opens", raw >= 0);
    check("an empty datagram is sent",
          ::sendto(raw, nullptr, 0, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0);

    const uint8_t behind[] = {'r', 'e', 'a', 'l'};
    check("  and a real one behind it",
          ::sendto(raw, reinterpret_cast<const char*>(behind), sizeof(behind), 0,
                   reinterpret_cast<sockaddr*>(&to), sizeof(to)) == sizeof(behind));

    // Loopback is quick but not instant; both must be queued before the drain
    // below, or this would pass by reading them on separate passes.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // One frame's worth: read until the socket says there is nothing more.
    std::vector<uint8_t> heard;
    NetAddress from;
    int delivered = 0;
    std::vector<uint8_t> last;
    while (server.receive(heard, from)) {
        ++delivered;
        last = heard;
    }

    check("the real datagram still comes out of that one drain", delivered == 1);
    check("  whole", last.size() == sizeof(behind)
                  && std::memcmp(last.data(), behind, sizeof(behind)) == 0);

#if defined(_WIN32)
    ::closesocket(raw);
#else
    ::close(raw);
#endif
}

// The join as NetSession actually plays it: greet() opens a peer connection and
// welcome() stamps its first sequence, so the one packet a never-heard peer
// could falsely acknowledge is exactly the one that proves it can receive.
void testAPeerThatHasHeardNothingAcknowledgesNothing() {
    std::printf("What a connection claims before it has heard anything:\n");

    NetConnection server, client;
    server.open(NetAddress{0x7F000001u, 40011});
    client.open(NetAddress{0x7F000001u, 40012});

    std::vector<uint8_t> wire, payload;
    std::vector<uint16_t> acknowledged;
    const uint8_t hello[] = {1, 2, 3};

    client.frame(hello, sizeof(hello), wire);
    check("the server takes a first hello",
          server.accept(wire.data(), wire.size(), payload, acknowledged));

    // The Welcome. Its sequence is what the server later reads as proof the
    // address can receive, so it has to be a packet, not a default.
    const uint16_t welcomeSequence = server.nextSequence();
    check("no packet is numbered the value that means nothing",
          welcomeSequence != NetConnection::NO_SEQUENCE);

    const uint8_t welcome[] = {9};
    std::vector<uint8_t> out;
    server.frame(welcome, sizeof(welcome), out);

    // Lost. The client says hello again - it still has heard nothing at all,
    // and every header it sends has to put something in the acknowledgement
    // field regardless.
    client.frame(hello, sizeof(hello), wire);
    acknowledged.clear();
    check("the server takes the second hello",
          server.accept(wire.data(), wire.size(), payload, acknowledged));

    check("a peer that has received nothing acknowledges nothing", acknowledged.empty());
    check("  so the packet proving it can receive stays unconfirmed",
          std::find(acknowledged.begin(), acknowledged.end(), welcomeSequence)
              == acknowledged.end());
    check("  and no round trip is invented from it", server.roundTrip() == 0.0f);
}

void testASpawnRefusesWhatItCannotMean() {
    std::printf("A spawn message, and the two values it must not carry:\n");

    NetSpawn spawn;
    spawn.slot        = 42;
    spawn.prefab      = "prefabs/crate.json";
    spawn.at.position = {3.0f, 4.0f, 5.0f};
    spawn.at.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    std::vector<uint8_t> bytes;
    BitWriter out(bytes, NetReliable::MAX_MESSAGE);
    check("an ordinary spawn is written", writeSpawn(out, spawn));
    out.finish();

    BitReader in(bytes.data(), bytes.size());
    check("  and its kind byte reads back", in.u8() == static_cast<uint8_t>(NetEvent::Spawn));

    NetSpawn back;
    check("  and it decodes", readSpawn(in, back));
    check("  to the same slot and path", back.slot == 42 && back.prefab == spawn.prefab);
    check("  at full precision, since nothing later corrects it",
          back.at.position.x == 3.0f && back.at.position.z == 5.0f);

    // A path the length field cannot describe. Written anyway, the length would
    // wrap and every byte after this message would be read at the wrong offset.
    NetSpawn tooLong = spawn;
    tooLong.prefab.assign(NET_PREFAB_PATH_MAX + 1, 'x');
    std::vector<uint8_t> refused;
    BitWriter out2(refused, NetReliable::MAX_MESSAGE);
    check("a path longer than the wire can name is refused", !writeSpawn(out2, tooLong));

    // The position is the one raw float in the protocol, so it is the one value
    // a sender can put anything at all into. A NaN reaches a Transform and
    // spreads to everything computed from it.
    std::vector<uint8_t> poisoned;
    BitWriter out3(poisoned, NetReliable::MAX_MESSAGE);
    check("a spawn is written to poison", writeSpawn(out3, spawn));
    out3.finish();

    // Overwrite the first coordinate in place: kind byte, slot, then the path.
    BitWriter forged(poisoned, NetReliable::MAX_MESSAGE);
    forged.u8(static_cast<uint8_t>(NetEvent::Spawn));
    forged.u32(spawn.slot);
    forged.bits(static_cast<uint32_t>(spawn.prefab.size()), 8);
    for (const char c : spawn.prefab) forged.bits(static_cast<uint8_t>(c), 8);
    forged.f32(std::numeric_limits<float>::quiet_NaN());
    forged.f32(4.0f);
    forged.f32(5.0f);
    Quantize::writeRotation(forged, spawn.at.rotation);
    forged.finish();

    BitReader poison(poisoned.data(), poisoned.size());
    check("the forged kind byte still reads", poison.u8() == static_cast<uint8_t>(NetEvent::Spawn));

    NetSpawn nowhere;
    check("a spawn at a position that is not a number is refused",
          !readSpawn(poison, nowhere));
}

void testBitsAreWrittenAndReadBack() {
    std::printf("Values packed to the bit:\n");

    std::vector<uint8_t> bytes;
    {
        BitWriter out(bytes, 64);
        out.boolean(true);
        out.bits(5u, 3);            // a value that does not fill its width
        out.bits(0x1FFFFu, 17);     // and one that fills it exactly
        out.u16(40000u);
        out.f32(-12.5f);
        out.boolean(false);
        check("nothing overflowed", !out.overflowed());
        check("  and it cost the bits it needed, not the bytes",
              out.bitCount() == 1 + 3 + 17 + 16 + 32 + 1);
        out.finish();
    }

    BitReader in(bytes.data(), bytes.size());
    check("a flag comes back", in.boolean());
    check("  a narrow field", in.bits(3) == 5u);
    check("  a full one", in.bits(17) == 0x1FFFFu);
    check("  a short", in.u16() == 40000u);
    check("  a float, exactly", in.f32() == -12.5f);
    check("  and the last flag", !in.boolean());
    check("and nothing ran off the end", !in.failed());

    // Reading past the end fails, and stays failed: a decoder tests once at the
    // end rather than after every field, so the failure has to stick.
    BitReader over(bytes.data(), bytes.size());
    for (int i = 0; i < 200; ++i) (void)over.u32();
    check("reading past the end fails", over.failed());

    // The property the whole packet budget rests on: a writer never writes past
    // what it was given, whatever it is asked for.
    std::vector<uint8_t> small;
    BitWriter tight(small, 4);
    for (int i = 0; i < 100; ++i) tight.u32(0xFFFFFFFFu);
    check("a writer cannot be pushed past its buffer", tight.overflowed());
    check("  and wrote no more than it was given", small.size() == 4);
}

void testQuantisedValuesSurviveTheRoundTrip() {
    std::printf("What a body's state costs on the wire:\n");

    constexpr float EXTENT = 512.0f;

    std::vector<uint8_t> bytes;
    const glm::vec3 position(123.456f, -7.891f, 0.0f);
    const glm::quat rotation = glm::normalize(glm::quat(0.31f, -0.44f, 0.72f, 0.15f));
    const glm::vec3 velocity(12.34f, -0.05f, 199.0f);

    {
        BitWriter out(bytes, 64);
        for (int i = 0; i < 3; ++i) Quantize::writePosition(out, position[i], EXTENT);
        Quantize::writeRotation(out, rotation);
        for (int i = 0; i < 3; ++i) Quantize::writeVelocity(out, velocity[i]);
        check("a full body state fits well inside a packet", !out.overflowed());
        out.finish();
    }

    BitReader in(bytes.data(), bytes.size());
    glm::vec3 backPosition;
    for (int i = 0; i < 3; ++i) backPosition[i] = Quantize::readPosition(in, EXTENT);
    const glm::quat backRotation = Quantize::readRotation(in);
    glm::vec3 backVelocity;
    for (int i = 0; i < 3; ++i) backVelocity[i] = Quantize::readVelocity(in);

    check("a position comes back inside a millimetre",
          glm::length(backPosition - position) < 0.002f);

    // Compared as a rotation rather than component by component, because q and
    // -q are the same rotation and the encoding is free to pick either.
    const float agreement = std::fabs(glm::dot(backRotation, rotation));
    check("a rotation comes back within a fifth of a degree", agreement > 0.9999f);

    check("a velocity comes back inside a centimetre a second",
          glm::length(backVelocity - velocity) < 0.02f);
    check("and the whole read landed", !in.failed());

    // The number the packet budget is built on.
    const size_t stateBits = 3u * Quantize::positionBits(EXTENT) + 2u + 3u * 9u + 3u * 16u;
    std::printf("      one body: %zu bits (%zu bytes), so %zu fit a 1200-byte packet\n",
                stateBits, (stateBits + 7u) / 8u, (1200u * 8u) / stateBits);
    // The figure the packet budget rests on, written as bodies per packet
    // rather than bits per body because that is what decides whether a scene
    // works: the same fields as raw floats are 62 bytes and fit nineteen.
    check("enough bodies fit one packet to describe a busy world",
          (1200u * 8u) / stateBits >= 60);

    // Boundaries, where a fixed-point encoder is most likely to be wrong.
    std::vector<uint8_t> edge;
    BitWriter out(edge, 32);
    Quantize::writePosition(out, -EXTENT, EXTENT);
    Quantize::writePosition(out,  EXTENT, EXTENT);
    Quantize::writePosition(out,  0.0f,   EXTENT);
    Quantize::writeVelocity(out,  Quantize::MAX_SPEED * 2.0f);   // past the top
    out.finish();

    BitReader back(edge.data(), edge.size());
    check("the far edge of the world round-trips",
          std::fabs(Quantize::readPosition(back, EXTENT) + EXTENT) < 0.002f);
    check("  and the other one", std::fabs(Quantize::readPosition(back, EXTENT) - EXTENT) < 0.002f);
    check("  and the origin is the origin", std::fabs(Quantize::readPosition(back, EXTENT)) < 0.002f);
    check("a speed past the top is clamped, not wrapped",
          Quantize::readVelocity(back) > Quantize::MAX_SPEED - 0.02f);
}

void testAnAddressIsReadTheWayAPlayerTypesIt() {
    std::printf("An address, as somebody would write it:\n");

    const NetAddress full = NetAddress::parse("127.0.0.1:27015");
    check("host and port", full && full.ipv4 == 0x7F000001u && full.port == 27015);
    check("  and reads back the same", full.toString() == "127.0.0.1:27015");

    // A player is told a machine, not a number - the port belongs to the game.
    const NetAddress bare = NetAddress::parse("127.0.0.1", 27015);
    check("a bare host takes the game's port", bare && bare.port == 27015);

    const NetAddress named = NetAddress::parse("localhost", 27015);
    check("a name is resolved", named && named.port == 27015);

    // Refused rather than half-read: each of these names a port the writer
    // meant, and quietly substituting the fallback would connect somewhere
    // they did not ask for.
    check("a trailing colon is not a host", !NetAddress::parse("127.0.0.1:", 27015));
    check("a port that is not a number is refused", !NetAddress::parse("127.0.0.1:abc", 27015));
    check("a port past sixteen bits is refused", !NetAddress::parse("127.0.0.1:70000", 27015));
    check("nothing at all is nowhere", !NetAddress::parse("", 27015));
    check("a host with no port and no fallback is nowhere",
          !NetAddress::parse("127.0.0.1"));
}

void testACommandedStepIsDeliveredWhole() {
    std::printf("A step that was asked for:\n");

    // Rates whose fixed step is not a binary fraction, which is most of them.
    // Handing the accumulator n * step seconds and letting it divide them back
    // out loses one at rate after rate: it need only come back a hair short.
    const uint32_t rates[] = { 60u, 64u, 100u, 128u, 250u };

    for (const uint32_t rate : rates) {
        Clock clock;
        clock.setTickRate(rate);
        clock.setPaused(true);
        clock.beginFrame();          // starts the clock; no wall time has passed

        clock.requestStep(20);
        clock.beginFrame();

        int delivered = 0;
        while (clock.consumeFixedStep()) ++delivered;

        char label[64];
        std::snprintf(label, sizeof(label), "  twenty asked at %u Hz is twenty run", rate);
        check(label, delivered == 20);
    }

    // The tick number is the count of steps handed out, so it cannot drift from
    // the loop it describes: there is one place that hands one out.
    Clock counted;
    counted.setTickRate(64);
    counted.setPaused(true);
    counted.beginFrame();

    check("no tick has run yet", counted.getTick() == 0);

    counted.requestStep(3);
    counted.beginFrame();
    uint32_t ran = 0;
    while (counted.consumeFixedStep()) ++ran;

    check("three steps run three ticks", ran == 3);
    check("  and the clock counted them", counted.getTick() == 3);

    // Paused with nothing asked for: no time accrues, so no tick runs.
    counted.beginFrame();
    check("a paused clock hands out nothing", !counted.consumeFixedStep());
    check("  and the tick stands still", counted.getTick() == 3);

    // A step survives the play state changing under it. It was asked for; which
    // way pause moved afterwards is not a reason to lose it.
    counted.requestStep(2);
    counted.setPaused(false);
    counted.setPaused(true);
    counted.beginFrame();
    ran = 0;
    while (counted.consumeFixedStep()) ++ran;
    check("a queued step outlives a pause toggle", ran == 2);
}

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

void testAProjectSurvivesBeingWritten() {
    std::printf("What a project.json keeps when a tool writes it back:\n");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "vkm_project_write_test";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);

    // Hand-authored, with a key this build has never heard of and a splash list
    // loadProject reads but saveProject deliberately does not write.
    {
        std::ofstream out(root / "project.json");
        out << R"({
    "name": "Written By Hand",
    "engineVersion": "0.0.1",
    "entryScene": "scenes/start.json",
    "tickRate": 90,
    "maxPlayers": 3,
    "netPort": 27888,
    "splash": [{"image": "logo.png", "seconds": 2.0}],
    "somethingThisBuildDoesNotKnow": {"kept": true}
})";
    }

    Project project;
    check("a hand-authored project reads", loadProject(root, project));
    check("with the seats it names", project.maxPlayers == 3);
    check("and the port", project.netPort == 27888);

    project.maxPlayers = 8;
    check("and writes back", saveProject(root, project));

    Project reread;
    check("what was written reads again", loadProject(root, reread));
    check("carrying the edit", reread.maxPlayers == 8);
    check("and everything that was not edited", reread.name == "Written By Hand"
                                             && reread.entryScene == "scenes/start.json"
                                             && reread.tickRate == 90
                                             && reread.netPort == 27888);

    // The two the writer must not lose: a key it does not model, and a list it
    // reads but never writes. A writer that rebuilt the document would drop
    // both, and the author would find out a week later.
    nlohmann::json doc;
    {
        std::ifstream in(root / "project.json");
        in >> doc;
    }
    check("a key this build does not model is still there",
          doc.contains("somethingThisBuildDoesNotKnow"));
    check("and the splash list it never writes",
          doc.contains("splash") && doc["splash"].is_array() && doc["splash"].size() == 1);

    std::filesystem::remove_all(root, ec);
}

void testTheSceneOfRecordStillOpens() {
    std::printf("The scene the engine keeps as its record of the format:\n");

    // engine.md calls obstacle_course.json the saved scene of record and asks
    // that a format change be round-tripped against it. Nothing does that
    // automatically, so a change that broke it would surface in the editor.
    const std::filesystem::path scene =
        std::filesystem::path(VKM_EXAMPLES_DIR) / "physics_lab" / "scenes" / "obstacle_course.json";

    std::error_code ec;
    if (!std::filesystem::exists(scene, ec)) {
        // A packaged SDK ships no examples/. Saying so beats failing on a file
        // that was never meant to be there.
        std::printf("      (no examples/ in this build - skipped)\n");
        return;
    }

    // This binary wires no asset factories, so every mesh and material the file
    // names resolves to nothing and says so. That is the format behaving as
    // designed - a reference that did not resolve is kept, not erased - and the
    // errors below belong to the load, not to a failure.
    std::printf("      (the unresolved-asset errors below are expected: no factories here)\n");

    Scene world;
    ResourceManager resources;
    check("the scene of record loads",
          SceneSerializer::load(world, resources, scene.string()));

    // What the lab is: four characters and the props they walk over. A load
    // that returned true having produced an empty world would pass the line
    // above and nothing else.
    size_t characters = 0;
    world.forEach<CharacterController, Transform>([&](EntityId, CharacterController&, Transform&) {
        ++characters;
    });
    check("carrying the four characters it is authored with", characters == 4);

    // Every character starts on something. A capsule authored in the air is a
    // character that falls on load, plays its jump clip while it does, and
    // reads as an animation bug rather than as the scene being wrong - which is
    // exactly how this was found.
    bool allResting = true;
    world.forEach<CharacterController, Collider>(
            [&](EntityId id, CharacterController&, Collider& collider) {
        const Transform& at = world.get<Transform>(id);
        for (const ColliderPart& part : collider.parts) {
            if (part.shape != ColliderShape::Capsule) continue;
            const float bottom = at.position.y + part.center.y - part.halfHeight - part.radius;
            if (bottom > 0.0f) allResting = false;
        }
    });
    check("each of them resting on the floor rather than above it", allResting);
}

int main() {
    // Engine code asserts and logs through vkmLog, so the logger has to exist
    // before a Scene does. ERROR level keeps expected noise out of the output,
    // and the temp directory keeps the file out of wherever ctest was run from.
    const std::filesystem::path logPath =
        std::filesystem::temp_directory_path() / "vkm_engine_tests.log";
    Vkm::Log::Logger::init(logPath.string(), "VKM_ENGINE-TESTS",
                           Vkm::Log::LogLevel::ERROR);

    testMathConvention();
    testBitsAreWrittenAndReadBack();
    testQuantisedValuesSurviveTheRoundTrip();
    testAnAddressIsReadTheWayAPlayerTypesIt();
    testTwoSocketsCanTalk();
    testAnEmptyDatagramDoesNotEndTheDrain();
    testAPeerThatHasHeardNothingAcknowledgesNothing();
    testASpawnRefusesWhatItCannotMean();
    testAConversationKnowsWhatArrived();
    testSequencesSurviveTheirOwnWrap();
    testTheSchemaCarriesComponentsThroughAScene();
    testTwoEndsRefuseToPlayDifferentGames();
    testTheCommonestValuesSurviveExactly();
    testABoneTheAnimationPlacesIsNotWorthAPacket();
    testTheFirstSnapshotIsTheWholeWorld();
    testALostSnapshotIsSaidAgain();
    testAnAcknowledgementOutOfOrderDoesNotUndoANewerOne();
    testABurstIsDeferredRatherThanDropped();
    testAnEntityThatLeavesTheWorldLeavesEveryClient();
    testOwnerOnlyStateGoesToItsOwnerAlone();
    testAWorldConvergesThroughALossyLink();
    testAJumpSurvivesTheNetworkLosingIt();
    testATickWithNoCommandStillRuns();
    testCommandsRunInTheOrderTheyWereMade();
    testWhatAFrameOfInputCostsOnTheWire();
    testAPacketCannotTalkTheDecoderPastTheEndOfACommand();
    testAMessageThatMustArriveDoes();
    testAMessageBlockRefusesWhatItCannotCarry();
    testSomethingBuiltAfterTheSceneLoadedReachesEveryone();
    testTwoSessionsPlayTheSameGame();
    testAClientBuiltFromDifferentSourceIsTurnedAway();
    testAFullServerSaysSoRatherThanIgnoring();
    testASnapshotNeverOutgrowsThePacketItRidesIn();
    testAClientThatLosesItsServerDoesNotInheritTheWorld();
    testAStutterDoesNotCostTheTicksItRan();
    testAServerThatHasBeenUpAWhileStillTakesAJoin();
    testStandingIsToldRatherThanGuessed();
    testAClientDoesNotMoveWhatItDoesNotOwn();
    testAClientPredictsWhatItIsPushing();
    testAShotIsJudgedAgainstWhatTheShooterCouldSee();
    testTheWorldIsDrawnSmoothlyBetweenWhatArrives();
    testSmoothingNeverFeedsItselfItsOwnGuess();
    testAPlayerIsNotDraggedBackwardsByTheirOwnConnection();
    testAPredictionThatWasWrongIsRunAgainRatherThanArguedWith();
    testACommandedStepIsDeliveredWhole();
    testTheSameWorldSimulatesTheSameWay();
    testAProjectSurvivesBeingWritten();
    testTheSceneOfRecordStillOpens();
    testCapsuleBoxFindsSmallOverlaps();
    testGjk();
    testRaycastShapes();
    testRaycastMisses();
    testRaycastFilters();
    testSpherecast();
    testCharacterStepUp();
    testTickRate();
    testInputCommand();
    testCommandCarriesTheView();
    testCharacterStaircase();
    testJointStiffnessIsIterationIndependent();
    testJointWakesASleeper();
    testDistanceJointPinnedToWorld();
    testRebuildMeshBvhWithoutMesh();
    testRestOnNarrowSupport();
    testPrefabKeepsItsJoints();
    testStepUpPastOwnBones();
    testJoints();
    testRagdoll();
    testMeshCollider();
    testMeshDoesNotEjectDownward();
    testOffsetMeshPartCollides();
    testImportedHierarchy();
    testComponentRoundTrip();
    testUnknownBehaviorsSurviveASave();
    testQueryAgainstNewShapes();
    testRagdollFallsTwice();
    testRagdollPose();
    testCollisionLayers();
    testSceneRoundTripKeepsReferences();
    testRagdollAsHitboxes();

    std::printf(g_failures ? "\n%d FAILURE(S)\n" : "\nALL OK\n", g_failures);
    return g_failures ? 1 : 0;
}
