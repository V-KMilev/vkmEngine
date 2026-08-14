#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "ecs/scene.h"
#include "ecs/component/camera.h"
#include "ecs/component/name.h"
#include "ecs/component/transform.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"

#include "potion_runner.h"

// Entries a host resolves after loading this module.
//
// vkmRegisterBehaviors is the required one: it populates the host's
// BehaviorRegistry so scenes can name this project's behaviors.
extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#endif
void vkmRegisterBehaviors() {
    Engine::BehaviorRegistry::get().registerBehavior<Engine::PotionRunner>();
}

// vkmBuildScene is the optional one. This game's world is generated, not
// authored: the persisted scene is a chase camera and one entity carrying the
// behavior, and every prop is built at play time. There is nothing for
// project.json's entryScene to point at, so the project says what it starts as
// here instead.
extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#endif
void vkmBuildScene(Engine::Scene& scene) {

        auto& registry = Engine::BehaviorRegistry::get();

    // A real night: the ambient is almost gone so the game's arch washes, trim
    // rims, headlights and glows carve visible pools out of the dark instead of
    // fighting a daylit scene. PotionRunner::buildWorld enforces the same mood at
    // play time, so the two never drift.
    scene.environment().intensity  = 0.08f;
    scene.environment().showSkybox = false;   // underground: tunnel dark, no sky

    // Chase camera, parked where PotionRunner drives it so the editor preview
    // already frames the track before play begins.
    auto camera = scene.createEntity();
    scene.add(camera, Engine::makeName("Camera"));
    scene.add(camera, Engine::Camera{Engine::ProjectionType::Perspective});
    scene.add(camera, Engine::Transform{
        glm::vec3(0.0f, 4.6f, -8.5f),
        glm::angleAxis(0.34f, Engine::Math::WORLD_AXIS_X),
        glm::vec3(1.0f)
    });

    // No sun: an underground night run, lit entirely by the game's own fixtures
    // (ceiling luminaires, neon trims, train headlights). With no directional
    // caster the 2D shadow atlas reserves no CSM layers, so the headlight spots
    // get all six slots; the two cube slots go to the ceiling lights nearest
    // the player (see PotionRunner::scrollWorld).

    // The whole game: one entity, one behavior, which builds the rest on Play.
    auto game = scene.createEntity();
    scene.add(game, Engine::makeName("PotionRunner"));
    scene.add(game, Engine::Transform{});
    Engine::ScriptComponent script;
    if (auto behavior = registry.create("PotionRunner")) {
        script.behaviors.push_back(std::move(behavior));
    }
    scene.add(game, std::move(script));
}
