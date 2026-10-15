#include <utility>

#include "ecs/scene.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"

#include "system/script/module_entry.h"

#include "city_block.h"

VKM_MODULE_ENTRY
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehaviors<City::CityBlock>();
}

// The city is generated from a fixed seed by the behavior, so there is no scene file.
VKM_MODULE_ENTRY
void vkmBuildScene(Vkm::Engine::Scene& scene, Vkm::Engine::ResourceManager& resources) {
    // Moved on the first tick; set so the editor frames a street before Play.
    auto camera = scene.createEntity();
    scene.add(camera, Vkm::Engine::makeName("Camera"));
    Vkm::Engine::Camera cameraComponent{Vkm::Engine::ProjectionType::Perspective};
    cameraComponent.zFar = City::CityBlock::CAMERA_FAR;
    scene.add(camera, std::move(cameraComponent));
    Vkm::Engine::Transform eye;
    eye.position = glm::vec3(0.0f, City::CityBlock::EYE_HEIGHT, 0.0f);
    scene.add(camera, std::move(eye));

    auto city = scene.createEntity();
    scene.add(city, Vkm::Engine::makeName("CityBlock"));
    scene.add(city, Vkm::Engine::Transform{});
    Vkm::Engine::addBehavior<City::CityBlock>(scene, city);
}
