#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/scene.h"
#include "ecs/component/camera.h"
#include "ecs/component/name.h"
#include "ecs/component/transform.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"

#include "stress_arena.h"

// Entries a host resolves after loading this module.
//
// vkmRegisterBehaviors is the required one: it populates the host's
// BehaviorRegistry so scenes can name this project's behaviors.
extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#endif
void vkmRegisterBehaviors() {
    Engine::BehaviorRegistry::get().registerBehavior<Engine::StressArena>();
}

// vkmBuildScene is the optional one. This project's world is generated rather
// than authored, so there is no scene file for project.json to point at - the
// arena is built from a fixed seed by the behavior below. Saying so here keeps
// the content with the project instead of in whatever executable loads it,
// which is what the --stress flag used to do.
extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#endif
void vkmBuildScene(Engine::Scene& scene) {

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
}
