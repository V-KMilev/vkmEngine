#pragma once

// Shared by the physics suites: the worlds they build and the loop that steps them.

#include "support.h"

#include "system/physics/authoring/mesh_collider.h"
#include "system/physics/collision/gjk.h"
#include "system/physics/collision/mesh_bvh.h"
#include "system/physics/collision/narrowphase.h"
#include "system/physics/collision/support.h"
#include "ecs/component/physics/character_controller.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/physics/character/character_controller_system.h"
#include "system/physics/physics_system.h"

// A static box: a floor or a kerb.
inline EntityId addBox(Scene& scene, const glm::vec3& center, const glm::vec3& halfExtents) {
    const EntityId id = scene.createEntity();

    Transform transform;
    transform.position = center;
    scene.add<Transform>(id, std::move(transform));

    Rigidbody body;
    body.motion = RigidbodyMotion::Static;
    scene.add<Rigidbody>(id, std::move(body));

    ColliderPart part;
    part.shape = ColliderShape::Box;
    part.halfExtents = halfExtents;
    Collider collider;
    collider.parts = { part };
    scene.add<Collider>(id, std::move(collider));

    return id;
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
    body.motion = RigidbodyMotion::Static;
    scene.add<Rigidbody>(id, std::move(body));

    ColliderPart part;
    part.shape = shape;
    Collider collider;
    collider.parts = { part };
    scene.add<Collider>(id, std::move(collider));

    return id;
}

// A capsule character standing with its feet on y = 0, walking along +X.
inline EntityId addCharacter(Scene& scene, float radius, float halfHeight, float stepHeight) {
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

// The run's meshes: a tick resolves a mesh collider's mesh by name, so a frame
// stepping a world with one must look here (TestFrame(scene, meshLibrary())).
inline ResourceManager& meshLibrary() {
    static ResourceManager s_meshes;
    return s_meshes;
}

// Physics, then the controller reading this tick's contacts, in setupEngineApp's
// Simulation-stage order.
inline void simulate(Scene& scene, int ticks) {
    TestFrame frame(scene, meshLibrary());
    FrameContext& ctx = frame.ctx;

    PhysicsSystem physics;
    CharacterControllerSystem controller;
    HierarchySystem hierarchy;

    for (int tick = 0; tick < ticks; ++tick) {
        physics.fixedUpdate(ctx);
        controller.fixedUpdate(ctx);
        // The Transform stage: keeps WorldTransform current for whatever reads it
        // after the step (physics walks parented bodies itself, via worldPoseOf).
        hierarchy.update(ctx);
    }
}

inline BoxShape boxAt(const glm::vec3& center, const glm::vec3& halfExtents) {
    BoxShape box;
    box.center = center;
    box.halfExtents = halfExtents;
    return box;
}

// A dynamic body with nothing holding it up, so only the joint is under test.
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

// A static body with no collider: something to hang from, not to hit.
inline EntityId addAnchor(Scene& scene, const glm::vec3& position) {
    const EntityId id = scene.createEntity();
    Transform transform;
    transform.position = position;
    scene.add<Transform>(id, std::move(transform));
    Rigidbody body;
    body.motion = RigidbodyMotion::Static;
    scene.add<Rigidbody>(id, std::move(body));
    return id;
}

// A flat grid of triangles: a floor that is not a box.
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
            mesh.indices.insert(
                mesh.indices.end(),
                {a, a + stride, a + 1, a + 1, a + stride, a + stride + 1}
            );
        }
    }
    return mesh;
}

// Adds @p mesh to meshLibrary() under a name of its own; returns its handle.
inline MeshHandle addTestMesh(const MeshAsset& mesh) {
    static uint32_t s_made = 0;
    MeshAsset copy = mesh;
    return meshLibrary().add(std::move(copy), "physics:mesh" + std::to_string(++s_made));
}

inline EntityId addMeshBody(Scene& scene, const MeshAsset& mesh) {
    const EntityId id = scene.createEntity();
    scene.add<Transform>(id, Transform{});

    Rigidbody body;
    body.motion = RigidbodyMotion::Static;
    scene.add<Rigidbody>(id, std::move(body));

    Collider collider;
    collider.parts.clear();   // the default unit box would be the thing hit
    addMeshCollider(collider, addTestMesh(mesh), meshLibrary());
    scene.add<Collider>(id, std::move(collider));

    return id;
}
