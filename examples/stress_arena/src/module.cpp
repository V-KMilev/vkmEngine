#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "ecs/scene.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"

#include "system/script/module_entry.h"

#include "stress_arena.h"

VKM_MODULE_ENTRY
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehaviors<Arena::StressArena>();
}

// The world is generated from a fixed seed by the behavior, so there is no scene file.
VKM_MODULE_ENTRY
void vkmBuildScene(Vkm::Engine::Scene& scene, Vkm::Engine::ResourceManager& resources) {
    // Overwritten on the first tick; set so the editor frames the arena before Play.
    auto camera = scene.createEntity();
    scene.add(camera, Vkm::Engine::makeName("Camera"));

    Vkm::Engine::Camera cameraComponent{Vkm::Engine::ProjectionType::Perspective};
    cameraComponent.zFar = Arena::StressArena::CAMERA_FAR;
    scene.add(camera, std::move(cameraComponent));

    // The half turn is forward being -Z where this art faces +Z.
    const glm::quat rotation = glm::quat(glm::vec3(glm::radians(9.0f), 0.0f, 0.0f))
        * glm::angleAxis(glm::pi<float>(), Vkm::Engine::Math::WORLD_AXIS_Y);
    scene.add(camera, Vkm::Engine::Transform{glm::vec3(0.0f, 26.0f, -95.0f), rotation, glm::vec3(1.0f)});

    auto arena = scene.createEntity();
    scene.add(arena, Vkm::Engine::makeName("StressArena"));
    scene.add(arena, Vkm::Engine::Transform{});
    Vkm::Engine::addBehavior<Arena::StressArena>(scene, arena);
}
