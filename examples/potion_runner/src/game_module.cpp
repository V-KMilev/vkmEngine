#include <utility>

#include <glm/glm.hpp>
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
    Vkm::Engine::BehaviorRegistry::get().registerBehavior<Vkm::Engine::PotionRunner>();
}

// This game's world is generated, not authored: the persisted scene is a chase
// camera and one entity carrying the behavior, and every prop is built at play
// time. There is nothing for project.json's entryScene to point at, so the
// project says what it starts as here instead.
VKM_MODULE_ENTRY
void vkmBuildScene(Vkm::Engine::Scene& scene) {

        auto& registry = Vkm::Engine::BehaviorRegistry::get();

    // A real night, so the game's own fixtures carve visible pools out of the
    // dark. PotionRunner::buildWorld sets the same values at play time, so the
    // authored scene and the built world cannot drift.
    scene.environment().sky.intensity  = 0.08f;
    scene.environment().sky.showSkybox = false;   // underground: tunnel dark, no sky

    // Chase camera, parked where PotionRunner drives it so the editor preview
    // already frames the track before play begins.
    auto camera = scene.createEntity();
    scene.add(camera, Vkm::Engine::makeName("Camera"));
    scene.add(camera, Vkm::Engine::Camera{Vkm::Engine::ProjectionType::Perspective});
    scene.add(camera, Vkm::Engine::Transform{
        glm::vec3(0.0f, 4.6f, -8.5f),
        // Looking down the track, which runs along +Z. The half turn is
        // forward being -Z; the track did not move.
        glm::angleAxis(0.34f, Vkm::Engine::Math::WORLD_AXIS_X)
            * glm::angleAxis(glm::pi<float>(), Vkm::Engine::Math::WORLD_AXIS_Y),
        glm::vec3(1.0f)
    });
    // The ear rides the eye. Nothing in the engine assumes that - a listener is
    // its own component so a game can put it elsewhere - but a chase camera is
    // where this game hears from.
    scene.add(camera, Vkm::Engine::AudioListener{});

    // No sun: an underground run lit entirely by the game's own fixtures. With no
    // directional caster the 2D atlas reserves no CSM layers, so the headlight
    // spots take all six slots and scrollWorld hands out the two cube ones.

    // The whole game: one entity, one behavior, which builds the rest on Play.
    auto game = scene.createEntity();
    scene.add(game, Vkm::Engine::makeName("PotionRunner"));
    scene.add(game, Vkm::Engine::Transform{});
    Vkm::Engine::ScriptComponent script;
    if (auto behavior = registry.create("PotionRunner")) {
        script.behaviors.push_back(std::move(behavior));
    }
    scene.add(game, std::move(script));
}
