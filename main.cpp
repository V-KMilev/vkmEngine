#include <iostream>
#include <cstdio>
#include <cstdint>
#include <chrono>

#include "logger.h"
#include "build_info.h"
#include "print_helper.h"

#include <vector>
#include "entity.h"
#include "component.h"

#include "event_manager.h"
#include "window_manager.h"
#include "statistics.h"
// #include "input_handle.h"

#include "glfw_include.h"

#include "gl_render.h"
#include "gl_debug.h"

#include <glm/glm.hpp>

#include "cpu_mesh.h"
#include "gpu_mesh.h"
#include "mesh.h"
#include "entity.h"

int main() {
    std::string rootDir = APP_ROOT_DIR;
    std::string logFile = rootDir + "/logs/log.log";
    Logger::init(logFile, "ENGINE", LogLevel::DEBUG);

    printBuildInfo();
    Core::enableGLDebugLogging(true);

    auto& windowManager = WindowManager::get();
    auto& eventManager = EventManager::get();
    auto& statisticTracker = StatisticTracker::get();

    windowManager.createWindow("VKM Engine");
    windowManager.updateMode(WindowMode::WINDOWED);
    windowManager.setFramerate(7000);

    Engine::CPUMesh mesh;
    mesh.loadFromFile("../not-real-file.obj");

    auto gpuMesh = std::make_shared<Engine::GPUMesh>(mesh);

    Core::Renderer renderer;
    renderer.setClearColor({0.1f, 0.1f, 0.1f, 1.0f});
    renderer.setDefaultState();

    Core::Shader shader("../shaders/basic");

    try {
        using clock = std::chrono::steady_clock;
        auto lastStatsPrint = clock::now();

        while (windowManager.beginFrame()) {
            // Update the input before everything else
            // Gives the lowest input latency
            if (!windowManager.updateInput()) break;

            eventManager.executeAsync();

            renderer.clearColor();
            renderer.clear();
            gpuMesh->draw(renderer, shader);

            // Swap the buffers and present the frame (applies frame limiting)
            if (!windowManager.swapBuffers()) break;

            statisticTracker.update();

            const auto now = clock::now();
            if (now - lastStatsPrint >= std::chrono::milliseconds(500)) {
                const auto& info = statisticTracker.getFrameInfo();
                printf("[%lu] FPS: %.2f (%.4fms) | Draws: %u | Entities: %u\n",
                    info.frameIndex,
                    info.frameRateInfo.frameRate,
                    info.frameRateInfo.frameTime,
                    info.renderSystemInfo.drawCalls,
                    info.entitySystemInfo.entityUpdates
                );
                fflush(stdout);
                lastStatsPrint = now;
            }
        }
    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
    } catch (...) {
        LOG_FATAL("Unknown exception");
    }

    LOG_INFO("Shutdown successfully!");
    return 0;
}
