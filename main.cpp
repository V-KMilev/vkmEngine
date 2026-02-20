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
#include "editor/camera_controller.h"

// Backend
#include "gl_backend.h"
#include "gl_forward_pass.h"
#include "gl_aabb_debug_pass.h"
#include "gl_grid_pass.h"
#include "gl_navigation_gizmo_pass.h"

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

        // Shaders
        Core::Shader pbr("../shaders/pbr");
        Core::Shader aabbDebug("../shaders/aabb_debug");
        Core::Shader gridShader("../shaders/grid");
        Core::Shader gizmoShader("../shaders/gizmo");

        // Render passes
        renderSystem.setBackend(std::make_unique<Engine::GLBackend>());
        renderSystem.addPass(std::make_unique<Engine::GLForwardPass>(pbr));
        // renderSystem.addPass(std::make_unique<Engine::GLAABBDebugPass>(aabbDebug));
        renderSystem.addPass(std::make_unique<Engine::GLGridPass>(gridShader));
        renderSystem.addPass(std::make_unique<Engine::GLNavigationGizmoPass>(gizmoShader));

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
