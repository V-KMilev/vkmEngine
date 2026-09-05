// The entry points a host looks for in a gameplay module. What each is for is
// documented where VKM_MODULE_ENTRY is - system/script/module_entry.h - so this
// file shows what to write rather than repeating why.
//
// One note the header cannot make: without vkmSetupNetwork, `vkm serve` refuses
// to start and the maxPlayers and netPort in project.json have nothing to act
// on. See docs/reference/system/networking.md.
#include "system/script/behavior_registry.h"
#include "ecs/scene.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/light.h"
#include "system/script/script_component.h"

#include "system/script/module_entry.h"

#include "game.h"

VKM_MODULE_ENTRY
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehavior<Game::Spinner>();
}

VKM_MODULE_ENTRY
void vkmBuildScene(Vkm::Engine::Scene& scene) {
    // Forward is -Z, so an unrotated camera at +Z faces the origin. Use
    // Math::computeForward to ask an orientation which way it points rather
    // than assuming; glm::quatLookAt aims the other way.
    const Vkm::Engine::EntityId camera = scene.createEntity();
    scene.add(camera, Vkm::Engine::makeName("Camera"));
    scene.add(camera, Vkm::Engine::Transform{
        {0.0f, 1.5f, 6.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}
    });
    scene.add(camera, Vkm::Engine::Camera{});

    // A directional light shines along its forward, so a sun needs a negative
    // pitch to come from above - the same -Z that puts the camera at +Z.
    const Vkm::Engine::EntityId sun = scene.createEntity();
    scene.add(sun, Vkm::Engine::makeName("Sun"));
    Vkm::Engine::Transform sunTransform{};
    sunTransform.rotation = glm::quat(glm::vec3(glm::radians(-50.0f), glm::radians(30.0f), 0.0f));
    scene.add(sun, sunTransform);
    Vkm::Engine::Light sunLight{};
    sunLight.type = Vkm::Engine::LightType::Directional;
    scene.add(sun, sunLight);

    const Vkm::Engine::EntityId spinner = scene.createEntity();
    scene.add(spinner, Vkm::Engine::makeName("Spinner"));
    scene.add(spinner, Vkm::Engine::Transform{});
    Vkm::Engine::ScriptComponent script{};
    script.behaviors.push_back(Vkm::Engine::BehaviorRegistry::get().create("Spinner"));
    scene.add(spinner, std::move(script));
}
