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
        Core::enableGLDebugLogging(true);

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
            window.getWindowContext(),
            sys.camera,
            sys.visibility,
            sys.render,
            sys.events
        );

        // Editor + render thread. ImGui's build phase (NewFrame + panels
        // + Render) stays on main inside EditorSystem::update; the draw
        // submission runs on the render thread via
        // EditorSystem::executeBackend inside the per-frame lambda.
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
