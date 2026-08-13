#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>
#include <string>

#include "logger.h"

#include "gl_debug.h"

#include "core/engine.h"
#include "debug/build_info.h"
#include "asset_registration.h"
#include "io/asset/asset_library.h"
#include "io/scene/scene_serializer.h"
#include "editor_system.h"
#include "system/script/script_module.h"
#include "platform/library/dynamic_library.h"
#include "app/engine_app.h"
#include "example/potion_scene.h"
#include "example/stress_scene.h"

int main(int argc, char** argv) {
    try {
        const std::string rootDir = APP_ROOT_DIR;
        const std::string logFile = rootDir + "/logs/log.log";

        if (!Logger::init(logFile, "VKM-ENGINE", LogLevel::TRACE)) {
            return -1;
        }

        Engine::printBuildInfo();
        // Async GL debug logging: catches and logs GL errors without forcing
        // GL_DEBUG_OUTPUT_SYNCHRONOUS, which validates every GL call on the
        // calling thread (a real CPU cost across pass + ImGui submission).
        // Pass true only to pin a GL error to its exact callsite.
        Core::enableGLDebugLogging(false);

        // The editor registers the cooked factory set plus the recipe factories
        // it needs to (re)cook assets from their source. Must precede scene I/O.
        Engine::registerCookedAssetFactories();
        Engine::registerRecipeAssetFactories();

        // Load the cooked asset database manifest (empty on a fresh project; the
        // cooker rebuilds it on save).
        Engine::AssetLibrary::get().load();

        // Load the hot-reloadable gameplay module: it registers behaviors into
        // the engine's registry (resolved from this exe). Declared before the
        // Engine so it outlives it - behaviors are destroyed during Engine
        // teardown and their code must still be loaded then. Must precede
        // setupEngineApp, whose default scene creates behaviors via the registry.
        Engine::ScriptModule scriptModule;
        const std::string modulePath =
            std::string(GAME_MODULE_DIR) + "/" + Engine::DynamicLibrary::platformName("game");
        if (!scriptModule.load(modulePath)) {
            LOG_ERROR("Game module failed to load from '%s' - scripts unavailable", modulePath.c_str());
        }

        Engine::Engine engine;

        // --stress boots the profiling load (example/stress_scene.h) instead of
        // the game. It is a flag rather than a scene file because the arena is
        // generated in code from a fixed seed - there is nothing on disk to load.
        const bool stress = argc > 1 && std::string(argv[1]) == "--stress";

        auto sys = setupEngineApp(engine, AppConfig{
            stress ? "VKM Engine (Stress)" : "VKM Engine (Editor)",
            true, false,
            stress ? generateStressArenaScene : generatePotionRunnerScene});

        engine.addSystem<Engine::EditorSystem>(Engine::SystemStage::UI,
            engine, engine.getWindow().getWindowContext(),
            sys.camera, sys.visibility, sys.render, scriptModule);

        if (argc > 1 && !stress) {
            const char* scenePath = argv[1];
            if (Engine::SceneSerializer::load(engine.getScene(), engine.getResources(), scenePath)) {
                LOG_INFO("Booted scene '%s'", scenePath);
            } else {
                LOG_ERROR("Failed to load scene '%s'; using the default scene", scenePath);
            }
        }

        engine.run();

    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        LOG_FATAL("Unknown exception");
        return EXIT_FAILURE;
    }

    LOG_INFO("Shutdown successfully!");
    return 0;
}
