#pragma once

#include <memory>
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
#include "ecs/component/transform.h"
#include "system/script/script_component.h"

#include "generator/light_generators.h"
#include "generator/material_generators.h"
#include "generator/mesh_generators.h"

#include "behaviors/cube_spinner.h"

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

    // A plain white PBR material for the centre cube.
    const auto cubeMat = Engine::generateDefaultMaterial(resources);
    resources.rename(cubeMat, "default");

    // Camera at an over-the-shoulder spot, looking at the origin.
    {
        const glm::vec3 camPos(3.0f, 2.5f, -5.0f);
        const glm::vec3 target(0.0f);
        auto e = scene.createEntity();
        scene.add(e, Engine::makeName("Camera"));
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
    scene.add(sun, Engine::makeName("Sun"));
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

    // Cube at the origin.
    auto cube = scene.createEntity();
    scene.add(cube, Engine::makeName("Cube"));
    scene.add(cube, Engine::Mesh{cubeMesh, cubeMat});
    scene.add(cube, Engine::Transform{
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f)
    });

    // Attach the example spinner so the cube spins in play mode (Phase 1
    // vertical slice). Restored to its authored orientation on Stop.
    {
        Engine::ScriptComponent script;
        script.behaviors.push_back(std::make_unique<Engine::CubeSpinner>());
        scene.add(cube, std::move(script));
    }

    // Return the active camera entity (the only one in the scene).
    Engine::Entity result{};
    scene.forEach<Engine::Camera>([&](Engine::EntityId id, const Engine::Camera&) {
        result = Engine::Entity{id};
    });
    return result;
}
