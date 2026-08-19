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
#include "io/project.h"
#include "io/project_paths.h"
#include "project_boot.h"
#include "editor_system.h"
#include "system/script/script_module.h"
#include "platform/library/dynamic_library.h"
#include "app/engine_app.h"

int main(int argc, char** argv) {
    try {
        // Project root, working directory and log file, in the one order that
        // works (see tools/project_boot.h).
        if (!Engine::bootHost(argc, argv, "log.log", "VKM-ENGINE")) return EXIT_FAILURE;

        // Async GL debug logging: catches and logs GL errors without forcing
        // GL_DEBUG_OUTPUT_SYNCHRONOUS, which validates every GL call on the
        // calling thread (a real CPU cost across pass + ImGui submission).
        // Pass true only to pin a GL error to its exact callsite.
        Core::enableGLDebugLogging(false);

        // The editor wires the recipe factories: they (re)cook assets from their
        // source and fall through to the cooked path for what is already baked.
        // Must precede scene I/O.
        Engine::registerRecipeAssetFactories();

        // Empty on a fresh project; the cooker rebuilds it on save.
        Engine::AssetLibrary::get().load();

        // Titles the window, so two editors on two projects are tellable apart.
        Engine::Project project;
        Engine::loadProject(Engine::ProjectPaths::projectRoot(), project);

        // Load the hot-reloadable gameplay module: it registers behaviors into
        // the engine's registry, so it must precede the scene boot. Declared
        // before the Engine so it outlives it - behaviors are destroyed during
        // Engine teardown and their code must still be loaded then.
        Engine::ScriptModule scriptModule;
        // The editor runs the edited project's code, from the one place a
        // project builds it - the same lookup File > Open Project does.
        const std::filesystem::path modulePath =
            Engine::ProjectPaths::projectBin() / Engine::DynamicLibrary::platformName("game");

        std::error_code moduleEc;
        if (!std::filesystem::exists(modulePath, moduleEc) ||
            !scriptModule.load(modulePath.string())) {
            LOG_WARNING("No gameplay module for this project - scripts unavailable");
        }

        Engine::Engine engine;


        // Same convention EditorSystem's per-frame title uses; it takes over on
        // the first frame and adds the scene.
        const std::string title = project.name + " - VKM Engine";
        auto sys = setupEngineApp(engine, AppConfig{
            title.c_str(),
            true, false});

        engine.addSystem<Engine::EditorSystem>(Engine::SystemStage::UI,
            engine.getWindow().getWindowContext(),
            sys.camera, sys.ui, sys.visibility, sys.render, scriptModule, project.name);

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
