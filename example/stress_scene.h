#pragma once

#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/engine.h"
#include "ecs/scene.h"
#include "ecs/component/camera.h"
#include "ecs/component/name.h"
#include "ecs/component/transform.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"

/**
 * @brief Seed the profiling scene: a camera plus the StressArena behavior.
 *
 * The persisted scene is deliberately two entities. Everything the profile
 * actually measures - thousands of drawables, hundreds of lights, the particle,
 * decal, probe, physics and UI load - is generated procedurally by the behavior
 * on the first play tick, from a fixed seed. Nothing is read from disk, so the
 * load does not depend on which assets a machine has cooked, and two captures of
 * the same build are comparable frame for frame.
 *
 * Run it with `engine_runtime --stress` (auto-plays) or `engine_editor --stress`
 * (press Play). Tune the load through the behavior's fields in the inspector, or
 * by editing the defaults in game/stress_arena.h; they are read once at build, so
 * a change takes effect on the next play.
 *
 * @return The camera entity, for CameraControllerSystem::setCameraEntity.
 */
inline Engine::Entity generateStressArenaScene(Engine::Engine& engine) {
    auto& scene    = engine.getScene();
    auto& registry = Engine::BehaviorRegistry::get();

    // The behavior overwrites these on the first tick; they are set here so the
    // editor frames the arena before Play rather than staring at the origin.
    auto camera = scene.createEntity();
    scene.add(camera, Engine::makeName("Camera"));

    Engine::Camera cameraComponent{Engine::ProjectionType::Perspective};
    cameraComponent.zFar = 600.0f;
    scene.add(camera, std::move(cameraComponent));

    // Parked outside the tower ring looking in. Positive pitch tilts the view
    // down: the engine's forward is +Z (Math::computeForward), not GLM's -Z.
    scene.add(camera, Engine::Transform{
        glm::vec3(0.0f, 26.0f, -95.0f),
        glm::quat(glm::vec3(glm::radians(9.0f), 0.0f, 0.0f)),
        glm::vec3(1.0f)
    });

    auto arena = scene.createEntity();
    scene.add(arena, Engine::makeName("StressArena"));
    scene.add(arena, Engine::Transform{});

    Engine::ScriptComponent script;
    if (auto behavior = registry.create("StressArena")) {
        script.behaviors.push_back(std::move(behavior));
    }
    scene.add(arena, std::move(script));

    return camera;
}
