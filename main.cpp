#include <iostream>
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <vector>

#include "logger.h"
#include "build_info.h"
#include "print_helper.h"

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

#include "cpu_transform.h"
#include "gpu_transform.h"
#include "cpu_camera.h"
#include "gpu_camera.h"

int main() {
    try {
        std::string rootDir = APP_ROOT_DIR;
        std::string logFile = rootDir + "/logs/log.log";

        if (!Logger::init(logFile, "ENGINE", LogLevel::DEBUG)) return -1;

        printBuildInfo();
        Core::enableGLDebugLogging(true);

        auto& windowManager = WindowManager::get();
        auto& eventManager = EventManager::get();
        auto& statisticTracker = StatisticTracker::get();

        windowManager.createWindow("VKM Engine");
        windowManager.updateMode(WindowMode::WINDOWED);
        windowManager.setFramerate(7000);

        // Create a CPU mesh and upload to GPU
        Engine::CPUMesh cpuMesh;
        cpuMesh.loadFromFile("../not-real-file.obj");
        auto gpuMesh = std::make_shared<Engine::GPUMesh>(cpuMesh);
        auto gpuMesh1 = std::make_shared<Engine::GPUMesh>(cpuMesh);

        // Create a simple transform and GPU uploader
        Engine::CPUTransform cpuTransform({glm::vec3(0.0f, 0.0f, 0.0f)});
        Engine::GPUTransform gpuTransform(cpuTransform);

        Engine::CPUTransform cpuTransform1({glm::vec3(0.0f, -2.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3{5.0f, 1.0f, 5.0f}});
        Engine::GPUTransform gpuTransform1(cpuTransform1);

        // Create a basic camera
        Engine::CPUCamera cpuCamera;
        cpuCamera.setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        Engine::GPUCamera gpuCamera(cpuCamera);

        Core::Renderer renderer;
        renderer.setClearColor({0.1f, 0.1f, 0.1f, 1.0f});
        renderer.setDefaultState();

        Core::Shader shader("../shaders/basic");

        using clock = std::chrono::steady_clock;
        auto lastStatsPrint = clock::now();

        while (windowManager.beginFrame()) {
            // Update the input before everything else
            // Gives the lowest input latency
            if (!windowManager.updateInput()) break;

            eventManager.executeAsync();

            // Animate the mesh transform
            static float angle = 0.0f;
            angle += 0.0003f;
            cpuTransform.setRotation(glm::vec3(angle, angle, 0.0f));

            cpuCamera.setPosition(glm::vec3(std::sin(angle) * 5.0f, std::sin(angle) * 5.0f, std::cos(angle) * 5.0f));

            renderer.clearColor();
            renderer.clear();

            shader.bind();

            // Upload transform to shader
            gpuCamera.upload(shader);

            gpuTransform.upload(shader);
            gpuMesh->draw(renderer, shader);

            gpuTransform1.upload(shader);
            gpuMesh1->draw(renderer, shader);

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
