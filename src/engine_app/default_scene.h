#pragma once

#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "core/engine.h"
#include "core/math/axes.h"
#include "ecs/scene.h"
#include "ecs/component/camera.h"
#include "ecs/component/collider.h"
#include "ecs/component/light.h"
#include "ecs/component/mesh.h"
#include "ecs/component/name.h"
#include "ecs/component/rigidbody.h"
#include "ecs/component/transform.h"

#include "generator/light_generators.h"
#include "generator/material_generators.h"
#include "generator/mesh_generators.h"

/**
 * @brief Minimal default scene: one camera, one directional light, one cube
 *        at the origin with a default white PBR material.
 *
 * Used as the engine's startup scene when no other scene file is loaded.
 * Returns the camera entity so the camera controller can be bound to it.
 */
namespace detail {
    /// Build a rotation such that Math::computeForward(rotation) == `forward`
    /// and Math::computeUp aligns with world up (with a fallback when forward
    /// is parallel to world up).
    inline glm::quat rotationFromForward(const glm::vec3& forward, const glm::vec3& worldUp = Engine::Math::WORLD_AXIS_Y_UP) {
        const glm::vec3 f = glm::normalize(forward);
        glm::vec3 r = glm::cross(worldUp, f);
        if (glm::length2(r) < 1e-6f) {
            // forward parallel to worldUp - pick any orthogonal axis.
            r = glm::cross(Engine::Math::WORLD_AXIS_X_RIGHT, f);
        }
        r = glm::normalize(r);
        const glm::vec3 u = glm::cross(f, r);
        return glm::quat_cast(glm::mat3(r, u, f));  // columns: right, up, forward
    }
}

inline Engine::Entity generateDefaultScene(Engine::Engine& engine) {
    auto& scene     = engine.getScene();
    auto& resources = engine.getResources();

    const auto cubeMesh = resources.add(Engine::generateCube(), "cube");
    const auto material = Engine::generateDefaultMaterial(resources);
    resources.edit(material).name = "default";

    // Camera at an over-the-shoulder spot, looking at the origin.
    {
        const glm::vec3 camPos(3.0f, 2.5f, -5.0f);
        const glm::vec3 target(0.0f);
        auto e = scene.createEntity();
        scene.add(e, Engine::Name{"Camera"});
        scene.add(e, Engine::Camera{Engine::ProjectionType::Perspective});
        scene.add(e, Engine::Transform{
            camPos,
            detail::rotationFromForward(target - camPos),
            glm::vec3(1.0f)
        });
    }

    // Directional sun light shining down + forward onto the cube. Forward
    // is the direction the light *travels*, so the Y component is negative.
    auto sun = scene.createEntity();
    scene.add(sun, Engine::Name{"Sun"});
    {
        auto light = Engine::generateDirectionalLight(
            glm::vec3(1.0f, 0.96f, 0.9f), 3.0f, true);
        scene.add(sun, light);
    }
    scene.add(sun, Engine::Transform{
        glm::vec3(0.0f, 5.0f, 0.0f),
        detail::rotationFromForward(glm::vec3(-0.4f, -1.0f, 0.3f)),
        glm::vec3(1.0f)
    });

    // The cube at the origin.
    auto cube = scene.createEntity();
    scene.add(cube, Engine::Name{"Cube"});
    scene.add(cube, Engine::Mesh{cubeMesh, material});
    scene.add(cube, Engine::Transform{
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f)
    });

    // A sphere next to the cube so the default scene shows curvature under
    // the PBR shading.
    {
        const auto sphereMesh = resources.add(Engine::generateSphere(32, 16), "sphere");
        auto sphere = scene.createEntity();
        scene.add(sphere, Engine::Name{"Sphere"});
        scene.add(sphere, Engine::Mesh{sphereMesh, material});
        scene.add(sphere, Engine::Transform{
            glm::vec3(3.0f, 0.0f, 0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec3(1.0f)
        });
    }

    // Physics demo: a static ground box with a few dynamic boxes and spheres
    // dropped above it. PhysicsSystem (fixedUpdate) integrates gravity, resolves
    // the collisions, and settles them into a resting stack. Every body is a
    // hierarchy root so its local Transform is its world pose.
    {
        const auto physSphere = resources.add(Engine::generateSphere(24, 12), "phys_sphere");

        // Ground: a wide thin box whose top surface sits at y = 0. The cube mesh
        // spans [-0.5, 0.5], so a Transform scale of (20,1,20) renders a slab
        // whose collider half-extents match exactly.
        auto ground = scene.createEntity();
        scene.add(ground, Engine::Name{"Ground"});
        scene.add(ground, Engine::Mesh{cubeMesh, material});
        scene.add(ground, Engine::Transform{
            glm::vec3(0.0f, -0.5f, 0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec3(20.0f, 1.0f, 20.0f)
        });
        Engine::Collider groundCol;
        groundCol.parts = { { glm::vec3(0.0f), glm::vec3(10.0f, 0.5f, 10.0f) } };
        scene.add(ground, groundCol);
        Engine::Rigidbody groundBody;
        groundBody.isStatic = true;
        scene.add(ground, groundBody);

        auto dropBox = [&](const char* name, const glm::vec3& pos, const glm::vec3& spin) {
            auto e = scene.createEntity();
            scene.add(e, Engine::Name{name});
            scene.add(e, Engine::Mesh{cubeMesh, material});
            scene.add(e, Engine::Transform{pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)});
            Engine::Collider col;
            scene.add(e, col);
            Engine::Rigidbody body;
            body.mass = 1.0f;
            body.restitution = 0.1f;
            body.angularVelocity = spin;
            scene.add(e, body);
        };

        auto dropSphere = [&](const char* name, const glm::vec3& pos) {
            auto e = scene.createEntity();
            scene.add(e, Engine::Name{name});
            scene.add(e, Engine::Mesh{physSphere, material});
            scene.add(e, Engine::Transform{pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)});
            Engine::Collider col;
            scene.add(e, col);
            Engine::Rigidbody body;
            body.mass = 1.0f;
            body.restitution = 0.4f;
            scene.add(e, body);
        };

        dropBox("Box A", glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f));
        dropBox("Box B", glm::vec3(0.25f, 4.4f, 0.15f), glm::vec3(0.0f, 0.0f, 1.5f));
        dropBox("Box C", glm::vec3(-0.15f, 6.0f, 0.1f), glm::vec3(1.0f, 0.0f, 0.0f));
        dropSphere("Sphere A", glm::vec3(2.0f, 5.0f, 0.0f));
        dropSphere("Sphere B", glm::vec3(-1.8f, 7.0f, 0.5f));
    }

    // Return the active camera entity (the only one in the scene).
    Engine::Entity result{};
    scene.forEach<Engine::Camera>([&](Engine::EntityId id, const Engine::Camera&) {
        result = Engine::Entity{id};
    });
    return result;
}
