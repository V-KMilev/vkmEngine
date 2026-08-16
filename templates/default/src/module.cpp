// The two entry points a host looks for in a gameplay module.
//
// vkmRegisterBehaviors is required: it is how the engine learns the names in a
// scene file map to your types. vkmBuildScene is optional - a project whose
// world is authored in the editor sets entryScene in project.json instead, and
// this function is then never called.
#include "system/script/behavior_registry.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "ecs/component/name.h"
#include "ecs/component/light.h"
#include "ecs/component/camera.h"
#include "system/script/script_component.h"
#include "resource/resource_manager.h"

#include "game.h"

// Reported back to the host at load: it refuses a module built against a
// different engine rather than letting a layout mismatch surface as a crash
// somewhere unrelated. VKM_ENGINE_VERSION comes from the engine you linked, so
// rebuilding against a new SDK is all this ever needs.
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
    Engine::BehaviorRegistry::get().registerBehavior<Game::Spinner>();
}

extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#endif
void vkmBuildScene(Engine::Scene& scene, Engine::ResourceManager& resources) {
    (void)resources;

    // Forward is +Z in this engine, so a camera at -Z looking along +Z faces the
    // origin. glm::quatLookAt aims the other way; do not reach for it here.
    const Engine::EntityId camera = scene.createEntity();
    scene.add(camera, Engine::makeName("Camera"));
    scene.add(camera, Engine::Transform{{0.0f, 1.5f, -6.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});
    scene.add(camera, Engine::Camera{});

    // A directional light needs a positive pitch to come from above, for the
    // same reason.
    const Engine::EntityId sun = scene.createEntity();
    scene.add(sun, Engine::makeName("Sun"));
    Engine::Transform sunTransform{};
    sunTransform.rotation = glm::quat(glm::vec3(glm::radians(50.0f), glm::radians(30.0f), 0.0f));
    scene.add(sun, sunTransform);
    Engine::Light sunLight{};
    sunLight.type = Engine::LightType::Directional;
    scene.add(sun, sunLight);

    const Engine::EntityId spinner = scene.createEntity();
    scene.add(spinner, Engine::makeName("Spinner"));
    scene.add(spinner, Engine::Transform{});
    Engine::ScriptComponent script{};
    script.behaviors.push_back(Engine::BehaviorRegistry::get().create("Spinner"));
    scene.add(spinner, std::move(script));
}
