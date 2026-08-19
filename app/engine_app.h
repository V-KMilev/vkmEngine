#pragma once

#include <filesystem>
#include <memory>
#include <system_error>

#include "core/engine.h"
#include "io/project_paths.h"

#include "system/camera/camera_controller_system.h"
#include "system/async/async_loader_system.h"
#include "system/script/behavior_system.h"
#include "system/sky/sky_system.h"
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

// Bake the default UI font ("ui:roboto") unless it is already present, so every
// UIText resolves its font by name. Startup-only: SceneSerializer swaps the
// FontAsset slot across a scene load's asset-graph swap (like shaders), so the
// bake is never repeated. The baker no-ops if the .ttf is gone.
inline void ensureDefaultUIFont(Vkm::Engine::ResourceManager& resources) {
    if (resources.findByName<Vkm::Engine::FontAsset>("ui:roboto")) return;
    Vkm::Engine::bakeFontSDF(resources,
        (Vkm::Engine::ProjectPaths::engineFonts() / "Roboto-Medium.ttf").string(),
        "ui:roboto");
}

// Per-binary policy for the shared bootstrap: everything that genuinely differs
// between vkm_editor and vkm_runtime, and nothing more - so the system
// stack itself is defined in exactly one place.
struct AppConfig {
    const char* windowTitle;
    bool        startPaused;
    bool        logFps;
};

// System handles the caller may still need after bootstrap. The editor feeds
// these into its EditorSystem; the runtime ignores the return value.
struct AppSystems {
    Vkm::Engine::CameraControllerSystem& camera;
    Vkm::Engine::UISystem&               ui;
    Vkm::Engine::VisibilitySystem&       visibility;
    Vkm::Engine::RenderSystem&           render;
};

// Stands a ready-to-run engine app up in `engine`: the window, the standard
// system stack, and the GL backend. The caller owns what differs per-binary -
// gameplay registration (must happen before this, so the scene that follows can
// create behaviors through the registry), the scene itself (bootProjectScene),
// any extra systems (the editor adds EditorSystem), and the run loop.
inline AppSystems setupEngineApp(Vkm::Engine::Engine& engine, const AppConfig& config) {
    // Bindings first: the systems below read input through named actions, and an
    // action with no binding is silently dead rather than an error.
    Vkm::Engine::installDefaultBindings(engine.getInput());
    auto& window = engine.getWindow();
    window.createWindow(config.windowTitle);
    window.setFramerate(0);
    // A game's own icon if it ships one, the engine's otherwise: a shipped game
    // should not wear the engine's logo, but one that authored no icon still
    // gets an icon rather than a blank.
    const std::filesystem::path projectIcon =
        Vkm::Engine::ProjectPaths::assets() / "logo" / "icon.png";
    std::error_code iconEc;
    window.setIcon(std::filesystem::exists(projectIcon, iconEc)
        ? projectIcon.string()
        : (Vkm::Engine::ProjectPaths::engineAssets() / "logo" / "vkm_engine_icon.png").string());

    auto& cameraController =
        engine.addSystem<Vkm::Engine::CameraControllerSystem>(Vkm::Engine::SystemStage::Input);
    engine.addSystem<Vkm::Engine::AsyncLoaderSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::BehaviorSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::AnimationSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::ParticleSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::PhysicsSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::SkySystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::HierarchySystem>(Vkm::Engine::SystemStage::Transform);
    auto& uiSystem = engine.addSystem<Vkm::Engine::UISystem>(Vkm::Engine::SystemStage::Transform);
    auto& visibilitySystem =
        engine.addSystem<Vkm::Engine::VisibilitySystem>(Vkm::Engine::SystemStage::Visibility);
    auto& renderSystem = engine.addSystem<Vkm::Engine::RenderSystem>(Vkm::Engine::SystemStage::Render);

    // The backend compiles its own shaders and owns its pass pipeline, so no
    // shader-asset registration or pass wiring is needed at the app level.
    renderSystem.setBackend(std::make_unique<Vkm::Engine::GLBackend>());

    ensureDefaultUIFont(engine.getResources());

    // No scene is seeded here: which one boots is the project's answer, given by
    // bootProjectScene after this returns. Seeding one would leave a stray
    // camera, light and cube underneath whatever the project then builds.

    engine.getClock().setPaused(config.startPaused);
    engine.setFPSLog(config.logFps);

    return AppSystems{cameraController, uiSystem, visibilitySystem, renderSystem};
}
