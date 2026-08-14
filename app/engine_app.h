#pragma once

#include <filesystem>
#include <memory>
#include <system_error>

#include "core/engine.h"
#include "io/project_paths.h"

#include "system/camera/camera_controller_system.h"
#include "system/async/async_loader_system.h"
#include "system/script/behavior_system.h"
#include "system/animation/animation_system.h"
#include "system/particle/particle_system.h"
#include "system/physics/physics_system.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/ui/ui_system.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "platform/input/default_bindings.h"

#include "gl_backend.h"

#include "resource/asset/font_asset.h"
#include "font/font_baker.h"

// Bake the default UI font ("ui:roboto") into @p resources unless it is already
// present, so every UIText resolves its font by name. Startup-only: scene loads
// carry the FontAsset slot across the asset-graph swap (SceneSerializer swaps it
// back, like shaders), so the bake never has to be repeated. The baker no-ops
// gracefully if the .ttf is gone.
inline void ensureDefaultUIFont(Engine::ResourceManager& resources) {
    if (resources.findByName<Engine::FontAsset>("ui:roboto")) return;
    Engine::bakeFontSDF(resources,
        (Engine::ProjectPaths::engineFonts() / "Roboto-Medium.ttf").string(),
        "ui:roboto");
}

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
    Engine::VisibilitySystem&       visibility;
    Engine::RenderSystem&           render;
};

// Stands a ready-to-run engine app up in `engine`: the window, the standard
// system stack, the GL backend, and the config's boot scene. The caller owns
// what differs per-binary - gameplay registration (must happen before this, so
// the scene that follows can create behaviors through the registry), the scene
// itself (bootProjectScene), any extra systems (the editor adds EditorSystem),
// and the run loop.
inline AppSystems setupEngineApp(Engine::Engine& engine, const AppConfig& config) {
    // Bindings first: the systems below read input through named actions, and an
    // action with no binding is silently dead rather than an error.
    Engine::installDefaultBindings(engine.getInput());
    auto& window = engine.getWindow();
    window.createWindow(config.windowTitle);
    window.setFramerate(0);
    // A game's own icon if it ships one, the engine's otherwise. Unlike the UI
    // font this is worth letting a project override - a shipped game should not
    // wear the engine's logo - but a project that has not authored one still
    // gets a window with an icon rather than a blank.
    const std::filesystem::path projectIcon =
        Engine::ProjectPaths::assets() / "logo" / "icon.png";
    std::error_code iconEc;
    window.setIcon(std::filesystem::exists(projectIcon, iconEc)
        ? projectIcon.string()
        : (Engine::ProjectPaths::engineAssets() / "logo" / "vkm_engine_icon.png").string());

    auto& cameraController = engine.addSystem<Engine::CameraControllerSystem>(Engine::SystemStage::Input);
    engine.addSystem<Engine::AsyncLoaderSystem>(Engine::SystemStage::Simulation);
    engine.addSystem<Engine::BehaviorSystem>(Engine::SystemStage::Simulation);
    engine.addSystem<Engine::AnimationSystem>(Engine::SystemStage::Simulation);
    engine.addSystem<Engine::ParticleSystem>(Engine::SystemStage::Simulation);
    engine.addSystem<Engine::PhysicsSystem>(Engine::SystemStage::Simulation);
    engine.addSystem<Engine::HierarchySystem>(Engine::SystemStage::Transform);
    engine.addSystem<Engine::UISystem>(Engine::SystemStage::Transform);
    auto& visibilitySystem = engine.addSystem<Engine::VisibilitySystem>(Engine::SystemStage::Visibility);
    auto& renderSystem     = engine.addSystem<Engine::RenderSystem>(Engine::SystemStage::Render);

    // The OpenGL backend compiles its own shaders (shaders/) and owns its
    // fixed forward pass pipeline - no shader-asset registration or pass
    // wiring is needed here at the app level.
    renderSystem.setBackend(std::make_unique<Engine::GLBackend>());

    // Bake the default UI font up front so every UIText - whether built by the
    // boot scene or loaded from a scene file - resolves its font by name.
    ensureDefaultUIFont(engine.getResources());

    // No scene is seeded here. Which one boots is the project's answer, given by
    // bootProjectScene after this returns; seeding one would leave a stray
    // camera, light and cube underneath whatever the project then builds. The
    // camera controller resolves whichever camera that scene marks active.

    engine.getClock().setPaused(config.startPaused);
    engine.setFPSLog(config.logFps);

    return AppSystems{cameraController, visibilitySystem, renderSystem};
}
