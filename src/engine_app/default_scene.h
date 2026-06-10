#pragma once

#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "core/engine.h"
#include "core/math/axes.h"
#include "ecs/scene.h"
#include "ecs/component/camera.h"
#include "ecs/component/light.h"
#include "ecs/component/mesh.h"
#include "ecs/component/name.h"
#include "ecs/component/reflection_probe.h"
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

    // A reflective centre material + two saturated cubes. The global IBL has
    // none of these colours, so a coloured reflection on the mirror cube is
    // unambiguously the reflection probe at work, not the environment map.
    const auto mirrorMat = Engine::generateDefaultMaterial(resources);
    { auto& m = resources.edit(mirrorMat); m.name = "mirror"; m.metallic = 1.0f; m.roughness = 0.08f; }
    const auto redMat = Engine::generateDefaultMaterial(resources);
    { auto& m = resources.edit(redMat); m.name = "red"; m.albedo = glm::vec4(1.0f, 0.04f, 0.04f, 1.0f); m.roughness = 0.6f; }
    const auto greenMat = Engine::generateDefaultMaterial(resources);
    { auto& m = resources.edit(greenMat); m.name = "green"; m.albedo = glm::vec4(0.05f, 1.0f, 0.08f, 1.0f); m.roughness = 0.6f; }

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

    // Reflective centre cube at the origin.
    auto cube = scene.createEntity();
    scene.add(cube, Engine::Name{"Mirror Cube"});
    scene.add(cube, Engine::Mesh{cubeMesh, mirrorMat});
    scene.add(cube, Engine::Transform{
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f)
    });

    // Saturated cubes flanking it - the local colour the probe captures.
    auto red = scene.createEntity();
    scene.add(red, Engine::Name{"Red Cube"});
    scene.add(red, Engine::Mesh{cubeMesh, redMat});
    scene.add(red, Engine::Transform{
        glm::vec3(3.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)
    });

    auto green = scene.createEntity();
    scene.add(green, Engine::Name{"Green Cube"});
    scene.add(green, Engine::Mesh{cubeMesh, greenMat});
    scene.add(green, Engine::Transform{
        glm::vec3(-3.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)
    });

    // Reflection probe above the centre cube; its box covers the trio so the
    // mirror cube samples the local (red / green) reflections inside it.
    auto probe = scene.createEntity();
    scene.add(probe, Engine::Name{"Reflection Probe"});
    scene.add(probe, Engine::ReflectionProbe{ glm::vec3(8.0f, 6.0f, 8.0f), 0.2f, 1.0f, 256 });
    scene.add(probe, Engine::Transform{
        glm::vec3(0.0f, 2.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)
    });

    // Return the active camera entity (the only one in the scene).
    Engine::Entity result{};
    scene.forEach<Engine::Camera>([&](Engine::EntityId id, const Engine::Camera&) {
        result = Engine::Entity{id};
    });
    return result;
}
