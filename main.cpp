#include <string>

#include "logger.h"
#include "debug/build_info.h"

#include "gl_debug.h"
#include "gl_context.h"
#include "gl_shader.h"

// Engine
#include "core/engine.h"
#include "system/animation/animation_system.h"
#include "system/event/event_system.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "system/camera/camera_controller.h"
#include "editor_system.h"

// Backend
#include "core/gl_backend.h"
#include "pass/gl_forward_pass.h"
#include "pass/gl_aabb_debug_pass.h"
#include "pass/gl_grid_pass.h"
#include "pass/gl_shadow_pass.h"

// Asset registration
#include "asset_registration.h"

// Default scene (cube at origin, sun, camera)
#include "examples/default_scene.h"

int main() {
    try {
        const std::string rootDir = APP_ROOT_DIR;
        const std::string logFile = rootDir + "/logs/log.log";

        if (!Logger::init(logFile, "ENGINE", LogLevel::TRACE)) {
            return -1;
        }

        Engine::printBuildInfo();
        Core::enableGLDebugLogging(true);

        // Register generators/loaders with the engine's asset factory registry
        // so SceneSerializer can recreate procedural meshes + folder materials
        // on cold-start scene loads.
        Engine::registerBuiltinAssetFactories();

        auto& engine = Engine::Engine::get();
        auto& window = engine.getWindow();

        window.createWindow("VKM Engine");
        window.setFramerate(0);

        // Systems - registered at the stage that matches their role.
        auto& cameraController = engine.addSystem<Engine::CameraController>(Engine::SystemStage::Input);
        auto& eventSystem      = engine.addSystem<Engine::EventSystem>     (Engine::SystemStage::Simulation);
        auto& animationSystem  = engine.addSystem<Engine::AnimationSystem> (Engine::SystemStage::Simulation);
        auto& hierarchySystem  = engine.addSystem<Engine::HierarchySystem> (Engine::SystemStage::Transform);
        auto& visibilitySystem = engine.addSystem<Engine::VisibilitySystem>(Engine::SystemStage::Visibility);
        auto& renderSystem     = engine.addSystem<Engine::RenderSystem>    (Engine::SystemStage::Render);
        engine.addSystem<Engine::EditorSystem>(Engine::SystemStage::UI,
            window.getWindowContext(), &cameraController, &visibilitySystem, &renderSystem, &eventSystem);

        // Shaders
        const std::string shaderDir = std::string(APP_ROOT_DIR) + "/shaders";
        Core::Shader pbr(shaderDir + "/pbr");
        Core::Shader unlit(shaderDir + "/unlit");
        Core::Shader aabbDebug(shaderDir + "/aabb_debug");
        Core::Shader gridShader(shaderDir + "/grid");
        Core::Shader shadowShader(shaderDir + "/shadow");
        // Render passes - shadow runs first so the forward pass can sample its result.
        renderSystem.setBackend(std::make_unique<Engine::GLBackend>());
        renderSystem.addPass(std::make_unique<Engine::GLShadowPass>(shadowShader));
        auto forwardPass = std::make_unique<Engine::GLForwardPass>(pbr);
        forwardPass->setShader(Engine::MaterialType::Unlit, unlit);
        renderSystem.addPass(std::move(forwardPass));
        auto aabbPass = std::make_unique<Engine::GLAABBDebugPass>(aabbDebug);
        aabbPass->setEnabled(false);
        renderSystem.addPass(std::move(aabbPass));
        renderSystem.addPass(std::make_unique<Engine::GLGridPass>(gridShader));

        // Default scene: a single cube at the origin under a directional
        // light. Scene/asset round-trip happy: every asset has a source
        // descriptor so save → cold-start load reproduces this exactly.
        auto cameraEntity = generateDefaultScene(engine);
        cameraController.setCameraEntity(cameraEntity);

        engine.run();

    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
    } catch (...) {
        LOG_FATAL("Unknown exception");
    }

    LOG_INFO("Shutdown successfully!");
    return 0;
}
