#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "logger.h"

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
        // Project root, working directory and log file, in the one order that
        // works (see tools/project_boot.h); argv[1] overrides the project beside
        // this executable, which is how one build runs several.
        if (!Vkm::Engine::bootHost(argc, argv, "log.log", "VKM-ENGINE")) return EXIT_FAILURE;

        const std::filesystem::path root = Vkm::Engine::ProjectPaths::projectRoot();
        std::error_code ec;

        // The runtime loads only cooked assets, so it registers just the cooked
        // factory set (no Assimp, no image decode). Must precede scene I/O.
        Vkm::Engine::registerCookedAssetFactories();

        // Resolves scene asset references to their cooked files on load.
        Vkm::Engine::AssetLibrary::get().load();

        // Declared before the Engine so it outlives it: behaviors are destroyed
        // during Engine teardown and their code must still be mapped then. It
        // fills the behavior registry, so it also precedes the scene boot below.
        Vkm::Engine::ScriptModule scriptModule;
        const std::filesystem::path modulePath =
            Vkm::Engine::ProjectPaths::projectBin() / Vkm::Engine::DynamicLibrary::platformName("game");

        // Fatal here where the editor only warns; see docs/reference/system/io.md,
        // "What each host does when a project will not open".
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

        engine.getClock().setTickRate(project.tickRate);

        // Anything but the project's own world leaves this game nothing to play:
        // its entry scene did not load, or it names none and its module builds
        // none, and both leave the runtime on the engine's default scene.
        if (Vkm::Engine::bootProjectScene(project, scriptModule,
                engine.getScene(), engine.getResources()).source != Vkm::Engine::SceneBoot::Project) {
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
