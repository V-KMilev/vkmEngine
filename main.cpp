#include <string>

#include "logger.h"
#include "debug/build_info.h"

#include "gl_debug.h"
#include "gl_context.h"
#include "gl_shader.h"

// Engine
#include "core/engine.h"
#include "animation/animation_system.h"
#include "event/event_system.h"
#include "visibility/visibility_system.h"
#include "render/render_system.h"
#include "camera_controller.h"
#include "editor_system.h"

// Backend
#include "core/gl_backend.h"
#include "pass/gl_forward_pass.h"
#include "pass/gl_aabb_debug_pass.h"
#include "pass/gl_grid_pass.h"

// Demo scene
#include "example/benchmark_scene.h"

int main() {
    try {
        const std::string rootDir = APP_ROOT_DIR;
        const std::string logFile = rootDir + "/logs/log.log";

        if (!Logger::init(logFile, "ENGINE", LogLevel::TRACE)) {
            return -1;
        }

        Engine::printBuildInfo();
        Core::enableGLDebugLogging(true);

        auto& engine = Engine::Engine::get();
        auto& window = engine.getWindow();

        window.createWindow("VKM Engine");
        window.setFramerate(0);

        // Systems
        auto& cameraController = engine.addSystem<Engine::CameraController>();
        auto& eventSystem      = engine.addSystem<Engine::EventSystem>();
        auto& visibilitySystem = engine.addSystem<Engine::VisibilitySystem>();
        auto& animationSystem  = engine.addSystem<Engine::AnimationSystem>();
        auto& renderSystem     = engine.addSystem<Engine::RenderSystem>();
        engine.addSystem<Engine::EditorSystem>(
            window.getWindowContext(), &cameraController, &visibilitySystem, &renderSystem);

        // Shaders
        const std::string shaderDir = std::string(APP_ROOT_DIR) + "/shaders";
        Core::Shader pbr(shaderDir + "/pbr");
        Core::Shader unlit(shaderDir + "/unlit");
        Core::Shader aabbDebug(shaderDir + "/aabb_debug");
        Core::Shader gridShader(shaderDir + "/grid");
        // Render passes
        renderSystem.setBackend(std::make_unique<Engine::GLBackend>());
        auto forwardPass = std::make_unique<Engine::GLForwardPass>(pbr);
        forwardPass->setShader(Engine::MaterialType::Unlit, unlit);
        renderSystem.addPass(std::move(forwardPass));
        auto aabbPass = std::make_unique<Engine::GLAABBDebugPass>(aabbDebug);
        aabbPass->setEnabled(false);
        renderSystem.addPass(std::move(aabbPass));
        renderSystem.addPass(std::make_unique<Engine::GLGridPass>(gridShader));

        // Scene setup
        BenchmarkConfig config;
        config.gridSize = 100;
        config.nearLayerCount = 500;
        config.midLayerCount = 1000;
        config.farLayerCount = 2000;
        config.animationRatio = 0.2f;
        config.pointLightCount = 16;
        config.spotLightCount = 8;
        config.useMeshVariety = true;
        config.useSphereDetail = true;

        auto cameraEntity = generateBenchmarkScene(engine, config);

        cameraController.setCameraEntity(cameraEntity);
        generateBenchmarkAnimations(engine.getScene());

        engine.run();

    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
    } catch (...) {
        LOG_FATAL("Unknown exception");
    }

    LOG_INFO("Shutdown successfully!");
    return 0;
}
