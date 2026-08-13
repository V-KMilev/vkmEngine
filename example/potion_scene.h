#pragma once

#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/engine.h"
#include "core/math/axes.h"
#include "ecs/scene.h"
#include "ecs/component/camera.h"
#include "ecs/component/name.h"
#include "ecs/component/transform.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"

/**
 * @brief Seed the Subway-Surfers-style endless runner scene.
 *
 * The persisted scene is deliberately tiny - a chase camera and one
 * logic entity carrying the PotionRunner behavior. Every visible prop (ground,
 * walls, player, obstacles, coins, stripes) and its mesh/material is generated
 * by the behavior at play time, so nothing here references a cooked asset and
 * the editor's play snapshot/restore stays trivial.
 *
 * Press Play (the editor starts paused; the runtime auto-plays) and run:
 * A/D or arrows switch lane, Space/W jump, R or Enter restart.
 *
 * @return The camera entity, for CameraControllerSystem::setCameraEntity.
 */
inline Engine::Entity generatePotionRunnerScene(Engine::Engine& engine) {
    auto& scene    = engine.getScene();
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
        glm::angleAxis(0.34f, Engine::Math::WORLD_AXIS_X_RIGHT),
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

    return camera;
}
