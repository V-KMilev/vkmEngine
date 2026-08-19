#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "logger.h"

#include "gl_debug.h"

#include "core/engine.h"
#include "asset_registration.h"
#include "io/asset/asset_library.h"
#include "platform/library/dynamic_library.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "project_boot.h"
#include "system/script/script_module.h"
#include "app/engine_app.h"

int main(int argc, char** argv) {
    try {
        // The project is the one beside this executable, unless an argument
        // names a different one - which is for running several projects out of
        // one build. Root, working directory and log file all get settled here,
        // in the one order that works (see tools/project_boot.h).
        if (!Engine::bootHost(argc, argv, "log.log", "VKM-ENGINE")) return EXIT_FAILURE;

        const std::filesystem::path root = Engine::ProjectPaths::projectRoot();
        std::error_code ec;

        // Async GL debug logging: catches and logs GL errors without forcing
        // GL_DEBUG_OUTPUT_SYNCHRONOUS, which validates every GL call on the
        // calling thread (a real CPU cost across draw submission). Pass true
        // only to pin a GL error to its exact callsite.
        Vkm::GL::enableGLDebugLogging(false);

        // The runtime loads only cooked assets, so it registers just the cooked
        // factory set (no Assimp, no image decode). Must precede scene I/O.
        Engine::registerCookedAssetFactories();

        // Resolves scene asset references to their cooked files on load.
        Engine::AssetLibrary::get().load();

        // Load the gameplay module rather than linking it, so a game is data
        // plus a module instead of a rebuilt engine. It registers behaviors into
        // the registry, so it must precede the scene boot. Declared before the
        // Engine so it outlives it - behaviors are destroyed during Engine
        // teardown and their code must still be mapped then. It lives in the
        // project's own bin/, the one place every project builds it.
        Engine::ScriptModule scriptModule;
        const std::filesystem::path modulePath =
            Engine::ProjectPaths::projectBin() / Engine::DynamicLibrary::platformName("game");

        if (!std::filesystem::exists(modulePath, ec) || !scriptModule.load(modulePath.string())) {
            LOG_WARNING("No gameplay module for this project - no behaviors available");
        }

        // Absent or unreadable leaves the defaults - a nameless project with no
        // entry scene - so a directory that is not a project still runs, on the
        // generated scene.
        Engine::Project project;
        Engine::loadProject(root, project);

        Engine::Engine engine;

        const std::string title = project.name;
        setupEngineApp(engine, AppConfig{
            title.c_str(),
            false, true});

        // A scene comes from the project that owns it, never from the command
        // line - one function so all three hosts open a project the same way
        // (see tools/project_boot.h).
        Engine::bootProjectScene(project, scriptModule,
                                 engine.getScene(), engine.getResources());

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
