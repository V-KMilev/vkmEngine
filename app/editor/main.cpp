#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>

#include "logger.h"

#include "core/engine.h"
#include "input/camera_controller_system.h"
#include "cook/recipe_registration.h"
#include "project_boot.h"
#include "editor_system.h"
#include "system/script/script_module.h"
#include "gl_backend.h"
#include "gl_debug.h"

#include "app/engine_app.h"

int main(int argc, char** argv) {
    try {
        // First: everything below logs to its file and resolves paths against its project.
        if (!Vkm::Engine::bootHost(argc, argv, "log.log", "VKM-ENGINE")) return EXIT_FAILURE;
        if (!Vkm::Engine::refuseExtraArgs(argc, argv, "vkm_editor")) return EXIT_FAILURE;

        // Before any scene I/O; see registerRecipeAssetFactories.
        Vkm::Engine::registerRecipeAssetFactories();

        // Declared before the Engine so it outlives it: behaviors are destroyed during
        // Engine teardown and need their code loaded. Filled by ProjectController::open.
        Vkm::Engine::ScriptModule scriptModule;

        Vkm::Engine::Engine engine;

        // The default title is a placeholder: syncWindowTitle in editor_system.cpp writes the real one.
        Vkm::App::AppConfig config;
        config.startPaused = true;
        auto sys = Vkm::App::setupEngineApp(engine, config);

        Vkm::GL::enableGLDebugLogging(false);
        sys.render.setBackend(std::make_unique<Vkm::Engine::GLBackend>(), engine.getWindow());

        // Registered by the authoring host only: right-drag grabs the pointer, which a
        // shipped game must not get unasked.
        auto& cameraController = engine.addSystem<Vkm::Engine::CameraControllerSystem>(
            Vkm::Engine::SystemStage::Input
        );

        engine.addSystem<Vkm::Engine::EditorSystem>(
            Vkm::Engine::SystemStage::Editor,
            engine.getWindow().getWindowContext(),
            cameraController,
            sys.render,
            engine.getRenderSettings(),
            sys.audio,
            sys.behaviors,
            scriptModule
        );

        // The project opens from EditorSystem::init; see docs/reference/editor.md,
        // "Opening a project".
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
