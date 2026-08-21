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
        if (!Vkm::Engine::bootHost(argc, argv, "log.log", "VKM-ENGINE")) return EXIT_FAILURE;

        const std::filesystem::path root = Vkm::Engine::ProjectPaths::projectRoot();
        std::error_code ec;

        // Async GL debug logging: catches and logs GL errors without forcing
        // GL_DEBUG_OUTPUT_SYNCHRONOUS, which validates every GL call on the
        // calling thread (a real CPU cost across draw submission). Pass true
        // only to pin a GL error to its exact callsite.
        Vkm::GL::enableGLDebugLogging(false);

        // The runtime loads only cooked assets, so it registers just the cooked
        // factory set (no Assimp, no image decode). Must precede scene I/O.
        Vkm::Engine::registerCookedAssetFactories();

        // Resolves scene asset references to their cooked files on load.
        Vkm::Engine::AssetLibrary::get().load();

        // Load the gameplay module rather than linking it, so a game is data
        // plus a module instead of a rebuilt engine. It registers behaviors into
        // the registry, so it must precede the scene boot. Declared before the
        // Engine so it outlives it - behaviors are destroyed during Engine
        // teardown and their code must still be mapped then. It lives in the
        // project's own bin/, the one place every project builds it.
        //
        // Fatal here, where the editor only warns: the behaviors in a scene are
        // created through the registry this fills, and a type nothing registered
        // is dropped on load without a word. Playing on would give a world that
        // draws and does nothing, and report success for it. The editor is the
        // tool you fix that in, so it opens anyway.
        Vkm::Engine::ScriptModule scriptModule;
        const std::filesystem::path modulePath =
            Vkm::Engine::ProjectPaths::projectBin() / Vkm::Engine::DynamicLibrary::platformName("game");

        if (!std::filesystem::exists(modulePath, ec)) {
            LOG_ERROR("No gameplay module at '%s' - build the project before playing it",
                      modulePath.string().c_str());
            return EXIT_FAILURE;
        }
        // The reason is already logged, and it is the reason that matters: built
        // against another engine version, missing its entry, or unreadable.
        if (!scriptModule.load(modulePath.string())) {
            LOG_ERROR("Gameplay module '%s' did not load", modulePath.string().c_str());
            return EXIT_FAILURE;
        }

        // Absent or unreadable leaves the defaults - a nameless project with no
        // entry scene - so a project.json that could not be read still plays, on
        // the world its module builds.
        Vkm::Engine::Project project;
        Vkm::Engine::loadProject(root, project);

        Vkm::Engine::Engine engine;

        const std::string title = project.name;
        setupEngineApp(engine, AppConfig{
            title.c_str(),
            false, true});

        // A scene comes from the project that owns it, never from the command
        // line - one function so all three hosts open a project the same way
        // (see tools/project_boot.h). Anything but the project's own world is a
        // failure to boot this game: either its entry scene did not load, or it
        // names none and its module builds none, and both leave the runtime
        // sitting on the engine's default scene under the game's own title.
        if (Vkm::Engine::bootProjectScene(project, scriptModule,
                engine.getScene(), engine.getResources()) != Vkm::Engine::SceneBoot::Project) {
            LOG_ERROR("Project '%s' has no world of its own to play", project.name.c_str());
            return EXIT_FAILURE;
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
