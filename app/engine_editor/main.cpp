#define VKM_LOG_CATEGORY "MAIN"

#include <string>

#include "logger.h"
#include "debug/build_info.h"

#include "gl_debug.h"

#include "core/engine.h"
#include "asset_registration.h"
#include "engine_app.h"
#include "editor_system.h"

int main() {
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

        // Asset factories must be registered before scene I/O can
        // recreate procedural meshes + folder materials on cold start.
        Engine::registerBuiltinAssetFactories();

        Engine::Engine engine;
        auto& window = engine.getWindow();
        window.createWindow("VKM Engine");
        window.setFramerate(0);

        auto sys = Engine::setupEngineApp(engine);

        // Editor system is the only thing this binary adds beyond the
        // shared bootstrap; that's the entire reason for the split.
        engine.addSystem<Engine::EditorSystem>(
            Engine::SystemStage::UI,
            engine,
            window.getWindowContext(),
            sys.camera,
            sys.visibility,
            sys.render,
            sys.events
        );

        // The editor opens in Edit mode: simulation is frozen until the user
        // presses Play in the viewport. The runtime binary never does this,
        // so it simulates immediately. See Engine::getSimulationClock.
        engine.getSimulationClock().setPaused(true);

        engine.logFPS(false);
        engine.run();

    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
    } catch (...) {
        LOG_FATAL("Unknown exception");
    }

    LOG_INFO("Shutdown successfully!");
    return 0;
}
