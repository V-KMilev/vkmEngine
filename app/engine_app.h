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
#include "system/animation/skeletal_animation_system.h"
#include "system/animation/bone_socket_system.h"
#include "system/audio/audio_system.h"
#include "system/particle/particle_system.h"
#include "system/physics/physics_system.h"
#include "system/physics/ragdoll_system.h"
#include "system/physics/character/character_controller_system.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/splash/splash_system.h"
#include "system/ui/ui_system.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "platform/input/default_bindings.h"

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

// The fade applies at each end of every entry in the sequence. The frames it
// runs over are the process's slowest, so a short fade arrives as a flash. A
// project's own logos carry their hold in its project.json.
constexpr float VENDOR_SPLASH_SECONDS = 0.5f;
constexpr float SPLASH_FADE_SECONDS   = 1.25f;

// Per-binary policy for the shared bootstrap: everything that genuinely differs
// between vkm_editor and vkm_runtime, and nothing more - so the system
// stack itself is defined in exactly one place.
struct AppConfig {
    const char* windowTitle;
    bool        startPaused;
    bool        logFps;

    /**
     * @brief Run with no window at all.
     *
     * For a host that referees rather than plays. The system stack is exactly
     * the same either way, and that is the point: an authority that simulated
     * differently from the clients it corrects would not be an authority. What
     * is skipped is the window, the icon and the GL debug switch - none of
     * which a simulation consults.
     *
     * The frame cap is not skipped with them. With no window there is no vsync
     * and nothing to block on, so a loop left uncapped spins a core as fast as
     * it can for frames nobody draws.
     */
    bool        headless = false;

    /**
     * @brief Frames a second a headless host holds itself to.
     *
     * Ignored when there is a window, which paces itself against the display.
     */
    uint32_t    headlessFrameRate = 128;
};

// System handles the caller may still need after bootstrap. The editor feeds
// these into its EditorSystem; the runtime ignores the return value.
struct AppSystems {
    Vkm::Engine::CameraControllerSystem& camera;
    Vkm::Engine::UISystem&               ui;
    Vkm::Engine::AudioSystem&            audio;
    Vkm::Engine::VisibilitySystem&       visibility;
    Vkm::Engine::RenderSystem&           render;
};

// Stands a ready-to-run engine app up in `engine`: the window and the standard
// system stack. The caller owns what differs per-binary - the render backend
// (or none, for a host that draws nothing), gameplay registration (must happen
// before this, so the scene that follows can create behaviors through the
// registry), the scene itself (bootProjectScene), any extra systems (the editor
// adds EditorSystem), and the run loop.
inline AppSystems setupEngineApp(Vkm::Engine::Engine& engine, const AppConfig& config) {
    // Bindings first: the systems below read input through named actions, and an
    // action with no binding is silently dead rather than an error.
    Vkm::Engine::installDefaultBindings(engine.getInput());
    auto& window = engine.getWindow();
    if (config.headless) {
        // No window, and therefore no vsync and nothing for swapBuffers to block
        // on - so the frame limiter is the only thing pacing the loop, and it is
        // the difference between a server and a busy loop.
        window.setFramerate(config.headlessFrameRate);
    } else {
        window.createWindow(config.windowTitle);
        window.setFramerate(0);
    }
    // A game's own icon if it ships one, the engine's otherwise: a shipped game
    // should not wear the engine's logo, but one that authored no icon still
    // gets an icon rather than a blank.
    const std::filesystem::path projectIcon =
        Vkm::Engine::ProjectPaths::assets() / "logo" / "icon.png";
    std::error_code iconEc;
    if (!config.headless) {
        window.setIcon(std::filesystem::exists(projectIcon, iconEc)
            ? projectIcon.string()
            : (Vkm::Engine::ProjectPaths::engineAssets() / "logo" / "vkm_engine_icon.png").string());
    }

    auto& cameraController =
        engine.addSystem<Vkm::Engine::CameraControllerSystem>(Vkm::Engine::SystemStage::Input);
    // First in the stage, so the frame it publishes is the one that frame's
    // render reads. It runs on the frame clock, not the simulation one: it fades
    // on while the rest is still loading, and while the editor's clock is paused.
    auto& splashSystem = engine.addSystem<Vkm::Engine::SplashSystem>(Vkm::Engine::SystemStage::Simulation);
    splashSystem.setFade(SPLASH_FADE_SECONDS);
    // The mono mark: the splash ground is black, and the light-background logo
    // is near-black ink on it. From the engine root, so a project with no assets
    // directory of its own still shows it.
    splashSystem.add(
        (Vkm::Engine::ProjectPaths::engineAssets() / "logo" / "vkm_engine_logo_mono.png").string(),
        VENDOR_SPLASH_SECONDS);
    engine.addSystem<Vkm::Engine::AsyncLoaderSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::BehaviorSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::AnimationSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::SkeletalAnimationSystem>(Vkm::Engine::SystemStage::Simulation);
    // After the pose it reads and before the bodies it writes, on the tick with
    // the solve rather than per frame with the pose: the bone transforms it
    // writes are what the solver reads next, so a per-frame write would vary a
    // tick's starting shape with the last frame's length.
    engine.addSystem<Vkm::Engine::RagdollSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::ParticleSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::PhysicsSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::CharacterControllerSystem>(Vkm::Engine::SystemStage::Simulation);
    engine.addSystem<Vkm::Engine::SkySystem>(Vkm::Engine::SystemStage::Simulation);
    // Ahead of HierarchySystem, not after it: a socket writes a local Transform
    // and the world resolve below turns it into a world matrix in the same frame,
    // which is what keeps an attachment on its bone rather than one frame behind.
    engine.addSystem<Vkm::Engine::BoneSocketSystem>(Vkm::Engine::SystemStage::Transform);
    engine.addSystem<Vkm::Engine::HierarchySystem>(Vkm::Engine::SystemStage::Transform);
    auto& uiSystem = engine.addSystem<Vkm::Engine::UISystem>(Vkm::Engine::SystemStage::Transform);
    // After the world resolve it reads poses from; see
    // docs/reference/system/audio.md, "Per-frame flow".
    auto& audioSystem = engine.addSystem<Vkm::Engine::AudioSystem>(Vkm::Engine::SystemStage::Transform);
    auto& visibilitySystem =
        engine.addSystem<Vkm::Engine::VisibilitySystem>(Vkm::Engine::SystemStage::Visibility);
    auto& renderSystem = engine.addSystem<Vkm::Engine::RenderSystem>(Vkm::Engine::SystemStage::Render);

    // Which backend a host draws with, or whether it draws at all, is the
    // host's choice: naming one here would bind every includer to it, including
    // the host that links none. RenderSystem::update returns without one.

    // A host that presents nothing needs neither: the font is baked from a file
    // in the engine's own asset directory, which a server shipped alone has no
    // reason to carry, and the device is a mixer thread for an empty room.
    if (config.headless) audioSystem.setSilent();
    else                 ensureDefaultUIFont(engine.getResources());

    // No scene is seeded here: which one boots is the project's answer, given by
    // bootProjectScene after this returns. Seeding one would leave a stray
    // camera, light and cube underneath whatever the project then builds.

    engine.getClock().setPaused(config.startPaused);
    engine.setFPSLog(config.logFps);

    return AppSystems{cameraController, uiSystem, audioSystem, visibilitySystem, renderSystem};
}
