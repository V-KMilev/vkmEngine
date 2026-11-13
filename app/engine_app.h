#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

#include "core/engine.h"
#include "io/project/roots.h"

#include "decoded_image.h"

#include "system/async_loader/async_loader_system.h"
#include "system/water/water_system.h"
#include "system/script/behavior_system.h"
#include "system/sky/sky_system.h"
#include "system/animation/animation_system.h"
#include "system/animation/skeletal_animation_system.h"
#include "system/animation/bone_socket_system.h"
#include "system/audio/audio_system.h"
#include "system/particle/particle_system.h"
#include "system/physics/physics_system.h"
#include "system/physics/ragdoll_system.h"
#include "system/physics/character_controller_system.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/terrain/terrain_system.h"
#include "system/loading/loading_system.h"
#include "system/splash/splash_system.h"
#include "system/ui/ui_system.h"
#include "system/visibility/visibility_system.h"
#include "system/debug_view/debug_view_system.h"
#include "system/render/render_system.h"

namespace Vkm::App {

// The fade runs over the process's slowest frames, so a short one arrives as a
// flash. A project's own logos carry their hold in its project.json.
constexpr float SPLASH_FADE_SECONDS = 1.0f;

/**
 * @brief Per-binary policy for the shared bootstrap.
 */
struct AppConfig {
    const char* windowTitle = "vkmEngine";
    bool        startPaused = false;

    uint32_t headlessFrameRate = 128;  ///< Frames a second a host with no window holds itself to; ignored
                                       ///< with one. The cap stands in for vsync: with no window, nothing
                                       ///< else blocks.
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
    Engine::LoadingSystem*  loading;  ///< None for a host with no window, which covers nothing.
};

/**
 * @brief Give @p window the project's icon, or the engine's when the project has none.
 *
 * @param window A window that exists.
 */
inline void setWindowIcon(Platform::WindowManager& window) {
    const std::filesystem::path projectIcon = Engine::Roots::assets() / "logo" / "icon.png";
    std::error_code ec;
    const std::filesystem::path icon = std::filesystem::exists(projectIcon, ec)
        ? projectIcon
        : Engine::Roots::engineAssets() / "logo" / "vkm_engine_icon.png";
    const Engine::DecodedImage image = Engine::decodeImageRGBA(icon.string());
    if (!image.isValid()) return;

    // Decoded bottom row first, the engine's order; a window takes the top row first.
    const size_t row = static_cast<size_t>(image.width) * 4;
    std::vector<uint8_t> topDown(image.pixels.size());
    for (size_t y = 0; y < image.height; ++y) {
        std::copy_n(image.pixels.data() + (image.height - 1 - y) * row, row, topDown.data() + y * row);
    }
    window.setIcon(topDown.data(), static_cast<int>(image.width), static_cast<int>(image.height));
}

/**
 * @brief Stand a ready-to-run engine app up in @p engine: the window and the standard system stack.
 *
 * The caller owns the rest: the render backend (or none) and the UI font it draws with
 * (ensureDefaultUIFont), gameplay registration before any scene I/O, the world
 * (bootProjectWorld), extra systems and the run loop.
 *
 * @param engine  The engine to populate.
 * @param config  What this host does differently.
 * @param surface What the host's render backend draws into the window through; none for a host with
 *                no window at all, which keeps the system stack and skips the window, its icon and
 *                the audio device.
 * @return The systems a host may still configure.
 */
inline AppSystems setupEngineApp(
    Engine::Engine& engine,
    const AppConfig& config,
    std::unique_ptr<Platform::WindowSurface> surface
) {
    const bool headless = surface == nullptr;
    auto& window = engine.getWindow();
    if (headless) {
        window.setFramerate(config.headlessFrameRate);
    } else {
        window.createWindow(config.windowTitle, std::move(surface));
        window.setFramerate(0);
        setWindowIcon(window);
    }

    auto& splashSystem = engine.addSystem<Engine::SplashSystem>(Engine::SystemStage::Simulation);
    splashSystem.setFade(SPLASH_FADE_SECONDS);
    // The engine's ident over its notice; from the engine root, so every project shows them.
    const std::filesystem::path logos = Engine::Roots::engineAssets() / "logo";
    splashSystem.addIdent(
        (logos / "vkm_engine_logo_mono.png").string(),
        (logos / "vkm_engine_notice.png").string()
    );
    // Before the loads land, so the frame a world arrives in is the first it covers; and before the
    // behaviors that load behind it. A host that presents nothing covers nothing, and loads at once.
    Engine::LoadingSystem* loadingSystem = nullptr;
    if (!headless) loadingSystem = &engine.addSystem<Engine::LoadingSystem>(Engine::SystemStage::Simulation);
    engine.addSystem<Engine::AsyncLoaderSystem>(Engine::SystemStage::Simulation);
    // Ahead of the behaviors that ask it and the bodies it floats, and of the cull and the renderer.
    engine.addSystem<Engine::WaterSystem>(Engine::SystemStage::Simulation);
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
    // Before RenderSystem, which hands the backend what it draws.
    engine.addSystem<Engine::DebugViewSystem>(Engine::SystemStage::Render);
    auto& renderSystem = engine.addSystem<Engine::RenderSystem>(Engine::SystemStage::Render);

    // A host that presents nothing needs no device, which would mix for an empty room.
    if (headless) audioSystem.setSilent();

    engine.getClock().setPaused(config.startPaused);

    return AppSystems{audioSystem, behaviorSystem, splashSystem, renderSystem, loadingSystem};
}

} // namespace Vkm::App
