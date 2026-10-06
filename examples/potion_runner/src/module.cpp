#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "ecs/scene.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"

#include "system/script/module_entry.h"

#include "potion_runner.h"

VKM_MODULE_ENTRY
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehaviors<Potion::PotionRunner>();
}

// This game's world is generated, not authored: the persisted scene is a chase
// camera and one entity carrying the behavior, and every prop is built at play
// time. There is nothing for project.json's entryScene to point at, so the
// project says what it starts as here instead.
VKM_MODULE_ENTRY
void vkmBuildScene(Vkm::Engine::Scene& scene, Vkm::Engine::ResourceManager& resources) {
    // A real night, so the game's own fixtures carve visible pools out of the
    // dark. The constants are the behavior's, which is what keeps this preview
    // and the running game the same look.
    scene.environment().sky.intensity  = Potion::PotionRunner::SKY_INTENSITY;
    scene.environment().sky.showSkybox = false;   // underground: tunnel dark, no sky

    // Chase camera, parked where PotionRunner drives it so the editor preview
    // already frames the track before play begins.
    auto camera = scene.createEntity();
    scene.add(camera, Vkm::Engine::makeName("Camera"));
    scene.add(camera, Vkm::Engine::Camera{Vkm::Engine::ProjectionType::Perspective});
    const glm::vec3 position(0.0f, Potion::PotionRunner::CAMERA_REST_Y, Potion::PotionRunner::CAMERA_BACK_Z);
    const float pitch = Potion::PotionRunner::CAMERA_PITCH;
    // Turned to look down the +Z track, since forward is -Z.
    const glm::quat rotation = glm::angleAxis(pitch, Vkm::Engine::Math::WORLD_AXIS_X)
        * glm::angleAxis(glm::pi<float>(), Vkm::Engine::Math::WORLD_AXIS_Y);
    scene.add(camera, Vkm::Engine::Transform{position, rotation, glm::vec3(1.0f)});
    scene.add(camera, Vkm::Engine::AudioListener{});

    // No sun: an underground run lit entirely by the game's own fixtures.

    // The whole game: one entity, one behavior, which builds the rest on Play.
    auto game = scene.createEntity();
    scene.add(game, Vkm::Engine::makeName("PotionRunner"));
    scene.add(game, Vkm::Engine::Transform{});
    Vkm::Engine::addBehavior<Potion::PotionRunner>(scene, game);
}
