#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/scene.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"

#include "stress_arena.h"

// Entries a host resolves after loading this module.
//
// vkmRegisterBehaviors is the required one: it populates the host's
// BehaviorRegistry so scenes can name this project's behaviors.
// Reported back to the host at load. It refuses a module built against a
// different engine rather than letting a layout mismatch surface later as a
// crash somewhere unrelated. VKM_ENGINE_VERSION comes from the engine this
// module linked, so rebuilding against a new SDK is all it ever needs.
extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#endif
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#endif
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehavior<Vkm::Engine::StressArena>();
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
void vkmBuildScene(Vkm::Engine::Scene& scene) {

        auto& registry = Vkm::Engine::BehaviorRegistry::get();

    // The behavior overwrites these on the first tick; they are set here so the
    // editor frames the arena before Play rather than staring at the origin.
    auto camera = scene.createEntity();
    scene.add(camera, Vkm::Engine::makeName("Camera"));

    Vkm::Engine::Camera cameraComponent{Vkm::Engine::ProjectionType::Perspective};
    cameraComponent.zFar = 600.0f;
    scene.add(camera, std::move(cameraComponent));

    // Parked outside the tower ring looking in. Positive pitch tilts the view
    // down: the engine's forward is +Z (Math::computeForward), not GLM's -Z.
    scene.add(camera, Vkm::Engine::Transform{
        glm::vec3(0.0f, 26.0f, -95.0f),
        glm::quat(glm::vec3(glm::radians(9.0f), 0.0f, 0.0f)),
        glm::vec3(1.0f)
    });

    auto arena = scene.createEntity();
    scene.add(arena, Vkm::Engine::makeName("StressArena"));
    scene.add(arena, Vkm::Engine::Transform{});

    Vkm::Engine::ScriptComponent script;
    if (auto behavior = registry.create("StressArena")) {
        script.behaviors.push_back(std::move(behavior));
    }
    scene.add(arena, std::move(script));
}
