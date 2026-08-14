#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>
#include <string>

#include "logger.h"

#include "gl_debug.h"

#include "core/engine.h"
#include "debug/build_info.h"
#include "asset_registration.h"
#include "io/asset/asset_library.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "project_boot.h"
#include "editor_system.h"
#include "system/script/script_module.h"
#include "platform/library/dynamic_library.h"
#include "app/engine_app.h"

int main(int argc, char** argv) {
    try {
        // Same rule as the runtime: the project is the one beside this
        // executable, unless an argument names a different one. Resolved before
        // any path is composed: the override takes effect at once, but a path
        // already built from the old root will not follow it.
        // A project browser, when it lands, is a GUI for naming one - not a
        // second way of opening it.
        bool argNotAProject = false;
        if (argc > 1) {
            std::error_code argEc;
            const std::filesystem::path found =
                Engine::findProjectRoot(std::filesystem::absolute(argv[1], argEc));
            if (found.empty()) argNotAProject = true;
            else                Engine::ProjectPaths::setProjectRoot(found);
        }

        // Pin the working directory to the ENGINE root, not the project's, for
        // the same reason the runtime does: shaders load CWD-relative and ship
        // with the engine, while everything a project owns is addressed
        // absolutely through ProjectPaths. Without this the editor only starts
        // when launched from the engine root.
        std::error_code cwdEc;
        std::filesystem::current_path(Engine::ProjectPaths::engineRoot(), cwdEc);

        const std::string rootDir = Engine::ProjectPaths::projectRoot().string();
        std::error_code logEc;
        std::filesystem::create_directories(rootDir + "/logs", logEc);
        const std::string logFile = rootDir + "/logs/log.log";

        if (!Logger::init(logFile, "VKM-ENGINE", LogLevel::TRACE)) {
            return -1;
        }


        // Deferred until the logger exists: a mistyped path would otherwise look
        // like it worked, but this is the first point anything can say so.
        if (argNotAProject) {
            LOG_WARNING("'%s' is not a project (no project.json in it or above it); "
                        "using the project beside this executable instead", argv[1]);
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

        // What the open project calls itself; titles the window so two editors
        // on two projects are tellable apart.
        Engine::Project project;
        Engine::loadProject(Engine::ProjectPaths::projectRoot(), project);

        // Load the hot-reloadable gameplay module: it registers behaviors into
        // the engine's registry (resolved from this exe). Declared before the
        // Engine so it outlives it - behaviors are destroyed during Engine
        // teardown and their code must still be loaded then. Must precede
        // setupEngineApp, whose default scene creates behaviors via the registry.
        Engine::ScriptModule scriptModule;
        // The editor edits a project, so it runs that project's code, from the
        // one place a project builds it - the same lookup File > Open Project
        // does when it switches.
        const std::filesystem::path modulePath =
            Engine::ProjectPaths::projectBin() / Engine::DynamicLibrary::platformName("game");

        std::error_code moduleEc;
        if (!std::filesystem::exists(modulePath, moduleEc) ||
            !scriptModule.load(modulePath.string())) {
            LOG_WARNING("No gameplay module for this project - scripts unavailable");
        }

        Engine::Engine engine;


        const std::string title = project.name + " - vkmEngine";
        auto sys = setupEngineApp(engine, AppConfig{
            title.c_str(),
            true, false});

        engine.addSystem<Engine::EditorSystem>(Engine::SystemStage::UI,
            engine, engine.getWindow().getWindowContext(),
            sys.camera, sys.visibility, sys.render, scriptModule);

        // The editor opens on the same scene the runtime would boot, by the
        // same rule (see tools/project_boot.h).
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
