#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>
#include <string>

#include "logger.h"

#include "gl_debug.h"

#include "core/engine.h"
#include "debug/build_info.h"
#include "asset_registration.h"
#include "game_behaviors.h"
#include "io/asset/asset_library.h"
#include "io/scene/scene_serializer.h"
#include "app/engine_app.h"

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
        // calling thread (a real CPU cost across draw submission). Pass true
        // only to pin a GL error to its exact callsite.
        Core::enableGLDebugLogging(false);

        // The runtime loads only cooked assets, so it registers just the cooked
        // factory set (no Assimp, no image decode). Must precede scene I/O.
        Engine::registerCookedAssetFactories();

        // The cooked asset database manifest resolves scene asset references to
        // their cooked files on load.
        Engine::AssetLibrary::get().load();

        // The runtime static-links the gameplay module (no hot-reload), so it
        // registers behaviors directly. Must precede setupEngineApp's default
        // scene, which creates behaviors through the registry.
        Engine::registerGameBehaviors();

        Engine::Engine engine;

        setupEngineApp(engine, AppConfig{"VKM Engine (Runtime)", false, true});

        if (argc > 1) {
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
