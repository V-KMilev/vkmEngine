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
#include "io/scene/scene_serializer.h"
#include "editor_system.h"
#include "system/script/script_module.h"
#include "platform/library/dynamic_library.h"
#include "app/engine_app.h"

int main(int argc, char** argv) {
    try {
        // The editor edits a project: name one to open it, or run with none and
        // edit whichever project the engine sits in. Resolved before any path is
        // composed, since projectRoot() caches what it first answers.
        if (argc > 1) {
            std::error_code argEc;
            const std::filesystem::path found =
                Engine::findProjectRoot(std::filesystem::absolute(argv[1], argEc));
            if (!found.empty()) Engine::ProjectPaths::setProjectRoot(found);
        }

        const std::string rootDir = Engine::ProjectPaths::projectRoot().string();
        std::error_code logEc;
        std::filesystem::create_directories(rootDir + "/logs", logEc);
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
        // The open project's module first, then one built beside this exe: the
        // editor edits a project, so it runs that project's code.
        const std::string moduleName = Engine::DynamicLibrary::platformName("game");
        const std::filesystem::path moduleCandidates[] = {
            Engine::ProjectPaths::projectBin() / moduleName,
            std::filesystem::path(GAME_MODULE_DIR) / moduleName,
        };

        bool moduleLoaded = false;
        for (const std::filesystem::path& candidate : moduleCandidates) {
            std::error_code moduleEc;
            if (!std::filesystem::exists(candidate, moduleEc)) continue;
            moduleLoaded = scriptModule.load(candidate.string());
            if (moduleLoaded) break;
        }
        if (!moduleLoaded) {
            LOG_WARNING("No gameplay module for this project - scripts unavailable");
        }

        Engine::Engine engine;


        const std::string title = project.name + " - vkmEngine";
        auto sys = setupEngineApp(engine, AppConfig{
            title.c_str(),
            true, false,
            nullptr});

        engine.addSystem<Engine::EditorSystem>(Engine::SystemStage::UI,
            engine, engine.getWindow().getWindowContext(),
            sys.camera, sys.visibility, sys.render, scriptModule);

        // An argument naming a scene file opens it; one naming a project has
        // already been resolved above into the project root.
        std::error_code sceneEc;
        if (argc > 1 && std::filesystem::is_regular_file(argv[1], sceneEc)) {
            const char* scenePath = argv[1];
            if (Engine::SceneSerializer::load(engine.getScene(), engine.getResources(), scenePath)) {
                LOG_INFO("Booted scene '%s'", scenePath);
            } else {
                LOG_ERROR("Failed to load scene '%s'; using the default scene", scenePath);
            }
        }

        // A project whose world is generated seeds it through its module, so the
        // editor opens on the same scene the runtime would boot.
        if (scriptModule.buildScene(engine.getScene())) {
            LOG_INFO("Scene built by the project's module");
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
