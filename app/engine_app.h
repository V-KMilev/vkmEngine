#pragma once

#include <algorithm>
#include <filesystem>
#include <memory>
#include <system_error>

#include "core/engine.h"
#include "io/project_paths.h"

#include "system/async/async_loader_system.h"
#include "system/ocean/ocean_system.h"
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
#include "system/terrain/terrain_system.h"
#include "system/splash/splash_system.h"
#include "system/ui/ui_system.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"

#include "ecs/component/ui/ui_text.h"
#include "resource/asset/font_asset.h"
#include "loader/font_baker.h"

namespace Vkm::App {

/**
 * @brief Bake the default UI font (DEFAULT_UI_FONT) unless it is already present.
 *
 * Startup-only: ResourceManager::swap keeps the FontAsset slot across scene
 * loads, so the size comes from the monitor, not the window, to cover any later
 * resize. An unreadable .ttf is logged and bakes nothing.
 *
 * @param resources     Receives the font.
 * @param displayHeight Monitor height in pixels; 0 when unknown bakes at the reference size.
 */
inline void ensureDefaultUIFont(Engine::ResourceManager& resources, int displayHeight) {
    if (resources.findByName<Engine::FontAsset>(Engine::DEFAULT_UI_FONT)) return;
    Engine::bakeFontSDF(
        resources,
        (Engine::ProjectPaths::engineFonts() / "Roboto-Medium.ttf").string(),
        Engine::DEFAULT_UI_FONT,
        Engine::uiFontPixels(displayHeight)
    );
}

// The fade runs over the process's slowest frames, so a short one arrives as a
// flash. A project's own logos carry their hold in its project.json.
constexpr float VENDOR_SPLASH_SECONDS = 0.5f;
constexpr float SPLASH_FADE_SECONDS   = 1.25f;

/**
 * @brief Per-binary policy for the shared bootstrap.
 */
struct AppConfig {
    const char* windowTitle = "vkmEngine";
    bool        startPaused = false;
    bool        logFps      = false;

    /**
     * @brief Frames a second a host with no window holds itself to; ignored with one.
     *
     * The cap stands in for vsync: with no window, nothing else blocks.
     */
    uint32_t headlessFrameRate = 128;
};

/**
 * @brief The system handles a host may still need after bootstrap.
 *
 * addSystem's return is the only handle Engine gives out.
 */
struct AppSystems {
    Engine::AudioSystem&    audio;
    Engine::BehaviorSystem& behaviors;
    Engine::SplashSystem&   splash;
    Engine::RenderSystem&   render;
};

/**
 * @brief Stand a ready-to-run engine app up in @p engine: the window and the standard system stack.
 *
 * The caller owns the rest: the render backend (or none), gameplay registration
 * before any scene I/O, the world (bootProjectWorld), extra systems and the run loop.
 *
 * @param engine  The engine to populate.
 * @param config  What this host does differently.
 * @param surface What the host's render backend draws into the window through; none for a host with
 *                no window at all, which keeps the system stack and skips the window, its icon, the
 *                UI font bake and the audio device.
 * @return The systems a host may still configure.
 */
inline AppSystems setupEngineApp(
    Engine::Engine& engine,
    const AppConfig& config,
    std::unique_ptr<Engine::WindowSurface> surface
) {
    const bool headless = surface == nullptr;
    auto&      window   = engine.getWindow();
    if (headless) {
        window.setFramerate(config.headlessFrameRate);
    } else {
        window.createWindow(config.windowTitle, std::move(surface));
        window.setFramerate(0);
    }
    const std::filesystem::path projectIcon = Engine::ProjectPaths::assets() / "logo" / "icon.png";
    std::error_code iconEc;
    if (!headless) {
        const std::filesystem::path icon = std::filesystem::exists(projectIcon, iconEc)
            ? projectIcon
            : Engine::ProjectPaths::engineAssets() / "logo" / "vkm_engine_icon.png";
        window.setIcon(icon.string());
    }

    auto& splashSystem = engine.addSystem<Engine::SplashSystem>(Engine::SystemStage::Simulation);
    splashSystem.setFade(SPLASH_FADE_SECONDS);
    // The mono mark, as the ground is black, over the engine's notice; from the engine root so every
    // project shows them.
    splashSystem.add(
        (Engine::ProjectPaths::engineAssets() / "logo" / "vkm_engine_logo_mono.png").string(),
        VENDOR_SPLASH_SECONDS,
        (Engine::ProjectPaths::engineAssets() / "logo" / "vkm_engine_notice.png").string()
    );
    engine.addSystem<Engine::AsyncLoaderSystem>(Engine::SystemStage::Simulation);
    // Ahead of the behaviors that float things on its sea, and of the cull and the renderer.
    engine.addSystem<Engine::OceanSystem>(Engine::SystemStage::Simulation);
    auto& behaviorSystem = engine.addSystem<Engine::BehaviorSystem>(Engine::SystemStage::Simulation);
    engine.addSystem<Engine::AnimationSystem>(Engine::SystemStage::Simulation);
    engine.addSystem<Engine::SkeletalAnimationSystem>(Engine::SystemStage::Simulation);
    // After the pose it reads and before the bodies it writes; see RagdollSystem.
    engine.addSystem<Engine::RagdollSystem>(Engine::SystemStage::Simulation);
    engine.addSystem<Engine::PhysicsSystem>(Engine::SystemStage::Simulation);
    engine.addSystem<Engine::CharacterControllerSystem>(Engine::SystemStage::Simulation);
    engine.addSystem<Engine::SkySystem>(Engine::SystemStage::Simulation);
    // Ahead of HierarchySystem, so an attachment's local Transform resolves this
    // frame rather than one behind its bone.
    engine.addSystem<Engine::BoneSocketSystem>(Engine::SystemStage::Transform);
    engine.addSystem<Engine::HierarchySystem>(Engine::SystemStage::Transform);
    engine.addSystem<Engine::TerrainSystem>(Engine::SystemStage::Transform);
    engine.addSystem<Engine::UISystem>(Engine::SystemStage::Transform);
    // After the world resolve; see docs/reference/audio.md, "Per-frame flow".
    auto& audioSystem = engine.addSystem<Engine::AudioSystem>(Engine::SystemStage::Transform);
    // After the world resolve too; see ParticleSystem.
    engine.addSystem<Engine::ParticleSystem>(Engine::SystemStage::Transform);
    engine.addSystem<Engine::VisibilitySystem>(Engine::SystemStage::Visibility);
    auto& renderSystem = engine.addSystem<Engine::RenderSystem>(Engine::SystemStage::Render);

    // A host that presents nothing needs neither: a lone server need not carry the
    // engine's font file, and the device would mix for an empty room.
    if (headless) audioSystem.setSilent();
    else          ensureDefaultUIFont(engine.getResources(), engine.getWindow().displayHeight());

    engine.getClock().setPaused(config.startPaused);
    engine.setFPSLog(config.logFps);

    return AppSystems{audioSystem, behaviorSystem, splashSystem, renderSystem};
}

} // namespace Vkm::App
