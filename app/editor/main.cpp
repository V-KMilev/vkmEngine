#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>

#include "logger.h"

#include "core/engine.h"
#include "asset_registration.h"
#include "project_boot.h"
#include "editor_system.h"
#include "system/script/script_module.h"
#include "app/engine_app.h"

int main(int argc, char** argv) {
    try {
        // Project root, working directory and log file, in the one order that
        // works (see tools/project_boot.h).
        if (!Vkm::Engine::bootHost(argc, argv, "log.log", "VKM-ENGINE")) return EXIT_FAILURE;

        // The editor wires the recipe factories: they (re)cook assets from their
        // source and fall through to the cooked path for what is already baked.
        // Must precede scene I/O.
        Vkm::Engine::registerRecipeAssetFactories();

        // Declared before the Engine so it outlives it: behaviors are destroyed
        // during Engine teardown and their code must still be loaded then. It is
        // filled when the editor opens a project, not here - one open sequence.
        Vkm::Engine::ScriptModule scriptModule;

        Vkm::Engine::Engine engine;

        // EditorSystem composes the real title - project, scene and modified
        // marker - from its first frame onward.
        auto sys = setupEngineApp(engine, AppConfig{"vkmEngine", true, false});

        engine.addSystem<Vkm::Engine::EditorSystem>(Vkm::Engine::SystemStage::UI,
            engine.getWindow().getWindowContext(),
            sys.camera, sys.ui, sys.visibility, sys.render, sys.audio, scriptModule);

        // The project opens from EditorSystem::init, on the same sequence File >
        // Open Project runs; see docs/reference/editor.md, "Opening a project".
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
