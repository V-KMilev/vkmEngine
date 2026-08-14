#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "logger.h"

#include "gl_debug.h"

#include "core/engine.h"
#include "debug/build_info.h"
#include "asset_registration.h"
#include "io/asset/asset_library.h"
#include "platform/library/dynamic_library.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "system/script/script_module.h"
#include "app/engine_app.h"

int main(int argc, char** argv) {
    try {
        const char* pathArg = argc > 1 ? argv[1] : nullptr;

        // An explicit argument names either a project directory or a file inside
        // one; either way the project is the nearest ancestor holding a
        // project.json. Done before anything composes a path, since projectRoot()
        // caches whatever it first resolves. Nothing is logged here - the logger
        // lives under the root we are still deciding.
        // Resolved against the launch directory now, because current_path() below
        // moves the CWD to the project root - a relative argument checked after
        // that would be measured from the wrong place.
        std::error_code argEc;
        const std::filesystem::path argPath = pathArg ? std::filesystem::absolute(pathArg, argEc) : std::filesystem::path{};

        if (!argPath.empty()) {
            const std::filesystem::path found = Engine::findProjectRoot(argPath);
            if (!found.empty()) Engine::ProjectPaths::setProjectRoot(found);
        }

        // Resolve the project root from the executable so a packaged build is
        // relocatable, and ensure logs/ exists - a shipped game has none yet and
        // Logger::init fails if it cannot open the file.
        const std::filesystem::path root = Engine::ProjectPaths::projectRoot();
        std::error_code ec;
        // Pin the working directory to that root: some subsystems (shader
        // loading) open CWD-relative paths, so without this the game only runs
        // when launched FROM the package root - double-clicking the exe in
        // bin/ would die on 'shaders/...' not existing.
        std::filesystem::current_path(root, ec);
        std::filesystem::create_directories(root / "logs", ec);
        const std::string logFile = (root / "logs" / "log.log").string();

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

        // Load the gameplay module rather than linking it: the same shared
        // library the editor loads, so a game is data plus a module instead of
        // a rebuilt engine. Declared before the Engine so it outlives it -
        // behaviors are destroyed during Engine teardown and their code must
        // still be mapped then. Must precede setupEngineApp's default scene,
        // which creates behaviors through the registry.
        // The project's own module first - a game brings its code with it - then
        // the one built beside this executable, which is what a development
        // checkout has.
        Engine::ScriptModule scriptModule;
        const std::string moduleName = Engine::DynamicLibrary::platformName("game");
        const std::filesystem::path candidates[] = {
            Engine::ProjectPaths::projectBin() / moduleName,
            std::filesystem::path(GAME_MODULE_DIR) / moduleName,
        };

        bool moduleLoaded = false;
        for (const std::filesystem::path& candidate : candidates) {
            if (!std::filesystem::exists(candidate, ec)) continue;
            moduleLoaded = scriptModule.load(candidate.string());
            if (moduleLoaded) break;
        }
        if (!moduleLoaded) {
            LOG_WARNING("No gameplay module for this project - no behaviors available");
        }

        // What the project says about itself. Absent or unreadable leaves the
        // defaults, which is a nameless project with no entry scene - so a
        // directory that is not a project still runs, on the generated scene.
        Engine::Project project;
        Engine::loadProject(root, project);

        Engine::Engine engine;

        const std::string title = project.name;
        setupEngineApp(engine, AppConfig{
            title.c_str(),
            false, true,
            nullptr});

        // What to boot, most specific first: a scene named on the command line,
        // then the project's entry scene, then the generated default that
        // setupEngineApp already built.
        std::filesystem::path scene;
        if (!argPath.empty() && std::filesystem::is_regular_file(argPath)) {
            scene = argPath;
        } else if (!project.entryScene.empty()) {
            scene = root / project.entryScene;
        }

        // A project whose world is generated says so in its module; one that
        // authored a scene names it in project.json. Neither is required - an
        // empty project boots an empty scene.
        if (scene.empty() && scriptModule.buildScene(engine.getScene())) {
            LOG_INFO("Scene built by the project's module");
        }

        if (!scene.empty()) {
            if (Engine::SceneSerializer::load(engine.getScene(), engine.getResources(), scene.string())) {
                LOG_INFO("Booted scene '%s'", scene.string().c_str());
            } else {
                LOG_ERROR("Failed to load scene '%s'; using the default scene", scene.string().c_str());
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
