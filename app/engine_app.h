#pragma once

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "core/engine.h"
#include "io/project_paths.h"

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

#include "ecs/component/ui/ui_text.h"
#include "resource/asset/font_asset.h"
#include "loader/font_baker.h"

namespace Vkm::App {

// The UI font's bake scales with the display height (a distance field magnifies
// well but not forever); the cap keeps the worst case a 2048-square atlas.
constexpr int   UI_FONT_REFERENCE_HEIGHT = 1080;
constexpr float UI_FONT_BASE_PIXELS      = 64.0f;
constexpr float UI_FONT_MAX_PIXELS       = 128.0f;

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

    const float height = static_cast<float>(displayHeight);
    const float wanted = displayHeight > 0
        ? UI_FONT_BASE_PIXELS * height / static_cast<float>(UI_FONT_REFERENCE_HEIGHT)
        : UI_FONT_BASE_PIXELS;

    Engine::bakeFontSDF(
        resources,
        (Engine::ProjectPaths::engineFonts() / "Roboto-Medium.ttf").string(),
        Engine::DEFAULT_UI_FONT,
        std::clamp(wanted, UI_FONT_BASE_PIXELS, UI_FONT_MAX_PIXELS)
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
     * @brief Run with no window at all.
     *
     * Same system stack; skips the window, its icon, the UI font bake and the
     * audio device. The frame cap stays: with no vsync, nothing else blocks.
     */
    bool headless = false;

    /**
     * @brief Frames a second a headless host holds itself to; ignored with a window.
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
 * @param engine The engine to populate.
 * @param config What this host does differently.
 * @return The systems a host may still configure.
 */
inline AppSystems setupEngineApp(Engine::Engine& engine, const AppConfig& config) {
    auto& window = engine.getWindow();
    if (config.headless) {
        window.setFramerate(config.headlessFrameRate);
    } else {
        window.createWindow(config.windowTitle);
        window.setFramerate(0);
    }
    const std::filesystem::path projectIcon = Engine::ProjectPaths::assets() / "logo" / "icon.png";
    std::error_code iconEc;
    if (!config.headless) {
        const std::filesystem::path icon = std::filesystem::exists(projectIcon, iconEc)
            ? projectIcon
            : Engine::ProjectPaths::engineAssets() / "logo" / "vkm_engine_icon.png";
        window.setIcon(icon.string());
    }

    auto& splashSystem = engine.addSystem<Engine::SplashSystem>(Engine::SystemStage::Simulation);
    splashSystem.setFade(SPLASH_FADE_SECONDS);
    // The mono mark, as the ground is black; from the engine root so every project shows it.
    splashSystem.add(
        (Engine::ProjectPaths::engineAssets() / "logo" / "vkm_engine_logo_mono.png").string(),
        VENDOR_SPLASH_SECONDS
    );
    engine.addSystem<Engine::AsyncLoaderSystem>(Engine::SystemStage::Simulation);
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
    engine.addSystem<Engine::UISystem>(Engine::SystemStage::Transform);
    // After the world resolve; see docs/reference/audio.md, "Per-frame flow".
    auto& audioSystem = engine.addSystem<Engine::AudioSystem>(Engine::SystemStage::Transform);
    // After the world resolve too; see ParticleSystem.
    engine.addSystem<Engine::ParticleSystem>(Engine::SystemStage::Transform);
    engine.addSystem<Engine::VisibilitySystem>(Engine::SystemStage::Visibility);
    auto& renderSystem = engine.addSystem<Engine::RenderSystem>(Engine::SystemStage::Render);

    // A host that presents nothing needs neither: a lone server need not carry the
    // engine's font file, and the device would mix for an empty room.
    if (config.headless) audioSystem.setSilent();
    else                 ensureDefaultUIFont(engine.getResources(), engine.getWindow().displayHeight());

    engine.getClock().setPaused(config.startPaused);
    engine.setFPSLog(config.logFps);

    return AppSystems{audioSystem, behaviorSystem, splashSystem, renderSystem};
}

} // namespace Vkm::App
