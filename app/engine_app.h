#pragma once

#include <memory>

#include "core/engine.h"
#include "io/project_paths.h"

#include "system/camera/camera_controller_system.h"
#include "system/event/event_system.h"
#include "system/async/async_loader_system.h"
#include "system/script/behavior_system.h"
#include "system/animation/animation_system.h"
#include "system/physics/physics_system.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"

#include "gl_backend.h"

#include "example/default_scene.h"

// Per-binary policy for the shared bootstrap: everything that genuinely differs
// between engine_editor and engine_runtime, and nothing more - so the system
// stack itself is defined in exactly one place.
struct AppConfig {
    const char* windowTitle;
    bool        startPaused;
    bool        logFps;
};

// System handles the caller may still need after bootstrap. The editor feeds
// these into its EditorSystem; the runtime ignores the return value.
struct AppSystems {
    Engine::CameraControllerSystem& camera;
    Engine::EventSystem&      events;
    Engine::VisibilitySystem& visibility;
    Engine::RenderSystem&     render;
};

// Stands a ready-to-run engine app up in `engine`: the window, the standard
// system stack, the GL backend, and the default scene. The caller owns what
// differs per-binary - gameplay registration (must happen before this so the
// default scene can create behaviors through the registry), any extra systems
// (the editor adds EditorSystem), scene-file overrides, and the run loop.
inline AppSystems setupEngineApp(Engine::Engine& engine, const AppConfig& config) {
    auto& window = engine.getWindow();
    window.createWindow(config.windowTitle);
    window.setFramerate(0);
    window.setIcon((Engine::ProjectPaths::assets() / "logo" / "vkm_engine_icon.png").string());

    auto& cameraController  = engine.addSystem<Engine::CameraControllerSystem> (Engine::SystemStage::Input);
    auto& eventSystem       = engine.addSystem<Engine::EventSystem>      (Engine::SystemStage::Simulation);
    auto& asyncLoaderSystem = engine.addSystem<Engine::AsyncLoaderSystem>(Engine::SystemStage::Simulation);
    auto& behaviorSystem    = engine.addSystem<Engine::BehaviorSystem>   (Engine::SystemStage::Simulation, eventSystem);
    auto& animationSystem   = engine.addSystem<Engine::AnimationSystem>  (Engine::SystemStage::Simulation);
    auto& physicsSystem     = engine.addSystem<Engine::PhysicsSystem>    (Engine::SystemStage::Simulation, eventSystem);
    auto& hierarchySystem   = engine.addSystem<Engine::HierarchySystem>  (Engine::SystemStage::Transform);
    auto& visibilitySystem  = engine.addSystem<Engine::VisibilitySystem> (Engine::SystemStage::Visibility);
    auto& renderSystem      = engine.addSystem<Engine::RenderSystem>     (Engine::SystemStage::Render);

    // The OpenGL backend compiles its own shaders (shaders/) and owns its
    // fixed 10-pass forward pipeline - no shader-asset registration or pass
    // pipeline wiring is needed here at the app level.
    renderSystem.setBackend(std::make_unique<Engine::GLBackend>());

    // Default scene: a single cube at the origin under a directional light.
    auto cameraEntity = generateDefaultScene(engine);
    cameraController.setCameraEntity(cameraEntity);

    engine.getSimulationClock().setPaused(config.startPaused);
    engine.logFPS(config.logFps);

    return AppSystems{cameraController, eventSystem, visibilitySystem, renderSystem};
}
