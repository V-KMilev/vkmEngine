#define VKM_LOG_CATEGORY "MAIN"

#include <string>

#include "logger.h"
#include "debug/build_info.h"

#include "gl_debug.h"

#include "core/engine.h"
#include "asset_registration.h"
#include "engine_app.h"

int main() {
    try {
        const std::string rootDir = APP_ROOT_DIR;
        const std::string logFile = rootDir + "/logs/log.log";

        if (!Logger::init(logFile, "VKM-ENGINE", LogLevel::TRACE)) {
            return -1;
        }

        Engine::printBuildInfo();
        Core::enableGLDebugLogging(true);

        // Asset factories must be registered before scene I/O can
        // recreate procedural meshes + folder materials on cold start.
        Engine::registerBuiltinAssetFactories();

        Engine::Engine engine;
        auto& window = engine.getWindow();
        window.createWindow("VKM Engine (Runtime)");
        window.setFramerate(0);

        // Standard engine bootstrap: systems, shaders, pipeline, default
        // scene. No editor is added - this binary is the foundation for a
        // shipped game executable and contains no editor code at all.
        Engine::setupEngineApp(engine);

        // Migrate the rendering backend's context to a dedicated render
        // thread once boot finishes. The main thread runs input,
        // simulation, animation, culling, and the next frame's view
        // build while the render thread is still drawing the previous
        // frame; the mutator phase waits before any resource write.
        engine.enableRenderThread(true);

        engine.run();

    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
    } catch (...) {
        LOG_FATAL("Unknown exception");
    }

    LOG_INFO("Shutdown successfully!");
    return 0;
}
