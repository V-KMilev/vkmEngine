#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "core/engine.h"
#include "core/math/axes.h"
#include "ecs/scene.h"
#include "ecs/component/camera.h"
#include "ecs/component/light.h"
#include "ecs/component/mesh.h"
#include "ecs/component/mesh_lod.h"
#include "ecs/component/name.h"
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
        light.shadowExtent = 10.0f;
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

    // LOD demo: a sphere with three procedural detail levels (32x16 / 16x8 /
    // 8x4 segments). RenderView picks a level by projected screen size, so
    // dollying away visibly drops its tessellation while the silhouette holds.
    // levels[0] is the same handle as the Mesh, so shadows and the no-LOD path
    // use the finest sphere.
    {
        const auto sphere0 = resources.add(Engine::generateSphere(32, 16), "sphere_lod0");
        const auto sphere1 = resources.add(Engine::generateSphere(16, 8),  "sphere_lod1");
        const auto sphere2 = resources.add(Engine::generateSphere(8, 4),   "sphere_lod2");

        Engine::MeshLOD lod{};
        lod.levels[0] = sphere0;
        lod.levels[1] = sphere1;
        lod.levels[2] = sphere2;
        lod.switchHeights[1] = 220.0f;  // below ~220 px tall -> level 1
        lod.switchHeights[2] = 70.0f;   // below ~70 px tall  -> level 2
        lod.count = 3;

        auto sphere = scene.createEntity();
        scene.add(sphere, Engine::Name{"LOD Sphere"});
        scene.add(sphere, Engine::Mesh{sphere0, material});
        scene.add(sphere, lod);
        scene.add(sphere, Engine::Transform{
            glm::vec3(3.0f, 0.0f, 0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec3(1.0f)
        });
    }

    // Return the active camera entity (the only one in the scene).
    Engine::Entity result{};
    scene.forEach<Engine::Camera>([&](Engine::EntityId id, const Engine::Camera&) {
        result = Engine::Entity{id};
    });
    return result;
}
