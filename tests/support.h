#pragma once

// Shared by every suite in this directory: the includes an engine test needs,
// the two assertions everything is written in, and the world-building helpers
// more than one suite reaches for.
//
// Everything here is inline because eight translation units include it. The
// failure count in particular has to be one variable and not one per file, or
// a suite could fail and the process still exit zero.

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
#include "platform/net/winsock_init.h"

// UdpSocket::send refuses a zero-length datagram on purpose - this end never
// emits one. A peer elsewhere is under no such rule, so exercising what happens
// when one arrives means sending it the way the network can.
#include "platform/windows_api.h"

#if !defined(_WIN32)
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif
#include "core/math/random.h"
#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "core/event/event_bus.h"
#include "core/host_chrome.h"
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
#include "system/physics/physics_events.h"
#include "resource/generate/mesh_generators.h"
#include "resource/generate/texture_generators.h"
#include "system/physics/collision/gjk.h"
#include "system/physics/collision/narrowphase.h"
#include "system/physics/collision/support.h"
#include "system/physics/query/query.h"
#include "system/script/script_component.h"

using namespace Vkm::Engine;

inline int g_failures = 0;

// A socket the suites open directly, for the datagrams UdpSocket refuses to
// emit. It is a signed descriptor on POSIX and an unsigned handle on Windows,
// where `>= 0` is a comparison the compiler can answer without looking.
#if defined(_WIN32)
    using RawSocket = SOCKET;
    inline bool rawSocketOpen(RawSocket s)  { return s != INVALID_SOCKET; }
    inline void rawSocketClose(RawSocket s) { ::closesocket(s); }
#else
    using RawSocket = int;
    inline bool rawSocketOpen(RawSocket s)  { return s >= 0; }
    inline void rawSocketClose(RawSocket s) { ::close(s); }
#endif

// Opened through here rather than by calling socket() at the site: on Windows
// the library has to be started before its first entry point, and a suite that
// happens to open a UdpSocket first is one edit away from not doing so.
inline RawSocket rawSocketUdp() {
    ensureWinsock();
    return ::socket(AF_INET, SOCK_DGRAM, 0);
}

/**
 * @brief The angle between two rotations, in degrees.
 *
 * Through the absolute dot product, because q and -q are the same rotation and
 * the wire encoding drops the sign. Comparing components directly, or taking
 * the angle of inverse(a)*b without the absolute value, reports 360 degrees for
 * two rotations that are identical.
 */
inline float rotationErrorDegrees(const glm::quat& a, const glm::quat& b) {
    const float dot = std::abs(glm::dot(glm::normalize(a), glm::normalize(b)));
    return glm::degrees(2.0f * std::acos(std::min(1.0f, dot)));
}

inline void check(const char* what, bool ok) {
    if (!ok) ++g_failures;
    std::printf("  %-62s %s\n", what, ok ? "ok" : "<-- FAILED");
}

// Distances and normals come out of divisions and square roots, so they are
// compared to a tolerance rather than for equality. It is far looser than the
// error being allowed for, because what these assert is which surface was hit,
// not how precisely it was located.
inline bool nearly(float a, float b) {
    return std::fabs(a - b) < 1e-3f;
}

// A static body with one collider part, which is all a query looks at.
inline EntityId addBody(
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

// A static box of a given size, which is what a floor and a kerb both are.
inline EntityId addBox(Scene& scene, const glm::vec3& center,
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
inline EntityId addCharacter(Scene& scene, float radius, float halfHeight,
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

// The services a FrameContext refers to, owned so a test can stand one up in a
// line. Every system takes a frame context, so every system test needs all of
// them live whether it cares about them or not; keeping the list here means a
// new field on FrameContext is one edit rather than one per suite.
struct TestFrame {
    ResourceManager resources;
    Clock           clock;
    EventBus        events;
    WindowManager   window;   // never given a window: nothing here draws
    InputMap        input;
    NetSession      ownedNet;
    HostChrome      chrome;
    FrameContext    ctx;

    explicit TestFrame(Scene& scene)
        : ctx{scene, resources, clock, events, window, input, ownedNet, chrome} {}

    // For the networking suites, where the session under test is built by the
    // test and the context has to refer to that one rather than to ownedNet.
    TestFrame(Scene& scene, NetSession& session)
        : ctx{scene, resources, clock, events, window, input, session, chrome} {}
};

// Drive the two systems the way the Simulation stage does - physics first, then
// the controller reading this tick's contacts - for long enough to walk into
// something two metres away.
inline void simulate(Scene& scene, int ticks) {
    TestFrame frame(scene);
    FrameContext& ctx = frame.ctx;

    PhysicsSystem physics;
    CharacterControllerSystem controller;
    HierarchySystem hierarchy;

    for (int tick = 0; tick < ticks; ++tick) {
        physics.fixedUpdate(ctx);
        controller.fixedUpdate(ctx);
        // The Transform stage, which the app runs every frame and a parented
        // body's pose resolves through: without it a ragdoll's bones, children of
        // the character, would be read in the wrong frame here and only here.
        hierarchy.update(ctx);
    }
}

inline bool sameDirection(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(a - b) < 1e-3f;
}

inline BoxShape boxAt(const glm::vec3& center, const glm::vec3& halfExtents) {
    BoxShape box;
    box.center = center;
    box.halfExtents = halfExtents;
    return box;
}

// A dynamic body with nothing holding it up, so what a joint does is the only
// thing under test.
inline EntityId addFallingBody(Scene& scene, const glm::vec3& position, float half) {
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
inline EntityId addAnchor(Scene& scene, const glm::vec3& position) {
    const EntityId id = scene.createEntity();
    Transform transform;
    transform.position = position;
    scene.add<Transform>(id, std::move(transform));
    Rigidbody body;
    body.isStatic = true;
    scene.add<Rigidbody>(id, std::move(body));
    return id;
}

// A rig shaped like something with limbs: a spine with two legs, which is the
// smallest skeleton where "did the joints hold it together" means anything.
inline SkeletonAsset makeTestRig() {
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

// A flat grid of triangles, which is what a floor is once it stops being a box.
inline MeshAsset makeGridMesh(int cells, float size) {
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

inline EntityId addMeshBody(Scene& scene, const MeshAsset& mesh) {
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

/**
 * @brief Children of @p entity, walked off the hierarchy's own linked list.
 *
 * Local to the tests: the engine has no caller for it.
 */
inline int childrenOf(const Scene& scene, EntityId entity) {
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
inline bool readSnapshotFrom(Scene& scene, const NetSchema& schema, const std::vector<uint8_t>& body) {
    BitReader reader(body.data(), body.size());
    return readSnapshot(scene, schema, reader);
}

/**
 * @brief A world of @p count crates in a line, each with a body.
 *
 * Returns their ids rather than letting a caller guess slots: slot zero is the reserved
 * null, so the first crate is not entity zero.
 */
inline std::vector<EntityId> buildCrates(Scene& scene, int count) {
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
inline void fillWorldSchema(NetSchema& schema) {
    schema.replicate<Transform>("Transform");
    schema.replicate<Rigidbody>("Rigidbody");
    schema.replicate<Ragdoll>("Ragdoll");
}

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
inline EntityId addWalker(Scene& scene) {
    const EntityId walker = scene.createEntity();
    Transform at;
    scene.add(walker, at);
    Rigidbody body;
    body.isKinematic = true;
    scene.add(walker, body);
    return walker;
}

inline InputCommand walkCommand(uint32_t sequence, uint32_t tick, float forward, bool jump) {
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

/**
 * @brief Pump both ends until @p done, or give up.
 *
 * Real sockets on loopback deliver within a frame, so a handful of rounds is generous;
 * the cap is there so a broken build fails the test instead of hanging the suite.
 */
template <typename Step, typename Done>

inline bool pumpUntil(Step step, Done done, int rounds = 40) {
    for (int i = 0; i < rounds; ++i) {
        step();
        if (done()) return true;
    }
    return false;
}
