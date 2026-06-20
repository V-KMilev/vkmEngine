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
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"

#include "generator/light_generators.h"
#include "generator/material_generators.h"
#include "generator/mesh_generators.h"

inline Engine::Entity generateDefaultScene(Engine::Engine& engine) {
    auto& scene     = engine.getScene();
    auto& resources = engine.getResources();
    auto& registry  = Engine::BehaviorRegistry::get();

    const auto cubeMesh = resources.add(Engine::generateCube(), "cube");
    const auto cubeMat  = Engine::generateDefaultMaterial(resources);

    // Camera at an over-the-shoulder spot, looking at the origin.
    auto camera = scene.createEntity();
    scene.add(camera, Engine::makeName("Camera"));
    scene.add(camera, Engine::Camera{Engine::ProjectionType::Perspective});
    scene.add(camera, Engine::Transform{
        glm::vec3(3.0f, 2.5f, -5.0f),
        glm::quat(glm::radians(glm::vec3(23.0f, -30.0f, 0.0f))),
        glm::vec3(1.0f)
    });

    // Directional sun light shining down + forward onto the cube. Forward
    // is the direction the light *travels*, so the Y component is negative.
    auto sun = scene.createEntity();
    auto light = Engine::generateDirectionalLight(glm::vec3(1.0f, 0.96f, 0.9f), 3.0f, true);
    scene.add(sun, Engine::makeName("Sun"));
    scene.add(sun, light);
    scene.add(sun, Engine::Transform{
        glm::vec3(0.0f, 5.0f, 0.0f),
        glm::quat(glm::radians(glm::vec3(30.0f, -130.0f, 0.0f))),
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

    // Attach example behaviors via the registry.
    Engine::ScriptComponent script;
    if (auto spinner = registry.create("CubeSpinner"))      script.behaviors.push_back(std::move(spinner));
    if (auto player  = registry.create("PlayerController")) script.behaviors.push_back(std::move(player));
    scene.add(cube, std::move(script));

    return camera;
}
