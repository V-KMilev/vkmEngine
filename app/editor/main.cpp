#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>

#include "vkm_log/logger.h"

#include "core/engine.h"
#include "view/camera_controller_system.h"
#include "import/recipe_registration.h"
#include "vkm_platform/process/discrete_gpu.h"
#include "font_baker.h"
#include "io/project/host_boot.h"
#include "editor_system.h"
#include "system/script/module/script_module.h"

#include "app/backend_choice.h"
#include "app/engine_app.h"

VKM_PLATFORM_PREFER_DISCRETE_GPU()

int main(int argc, char** argv) {
    const int status = Vkm::Engine::runHost([&] {
        // First: everything below logs to its file and resolves paths against its project.
        Vkm::Engine::bootHost(argc, argv, "log.log", "VKM-ENGINE");
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
        auto sys = Vkm::App::setupEngineApp(engine, config, Vkm::App::EditorBackend::windowSurface());
        Vkm::Engine::ensureDefaultUIFont(engine.getResources(), engine.getWindow().displayHeight());

        sys.render.setBackend(std::make_unique<Vkm::App::EditorBackend>(), engine.getWindow());

        // Registered by the authoring host only: right-drag grabs the pointer, which a
        // shipped game must not get unasked.
        auto& cameraController = engine.addSystem<Vkm::Engine::CameraControllerSystem>(
            Vkm::Engine::SystemStage::Input
        );

        engine.addSystem<Vkm::Engine::EditorSystem>(
            Vkm::Engine::SystemStage::Editor,
            engine.getWindow(),
            cameraController,
            sys.render,
            sys.audio,
            *sys.loading,
            sys.behaviors,
            scriptModule
        );

        // The project opens from EditorSystem::init; see docs/reference/editor.md,
        // "Opening a project".
        engine.run();

        return EXIT_SUCCESS;
    });
    // After the engine's teardown, which ends the run.
    if (status == EXIT_SUCCESS) LOG_INFO("Shutdown successfully!");
    return status;
}
