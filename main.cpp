#include <cstdio>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "logger.h"
#include "build_info.h"
#include "print_helper.h"

#include "gl_debug.h"
#include "gl_context.h"
#include "gl_shader.h"

#include "statistics.h"
#include "event_manager.h"
#include "window_manager.h"
// #include "input_handle.h"

#include "camera.h"
#include "mesh.h"

#include "render_manager.h"

#include "gl_backend.h"
#include "gl_forward_pass.h"

#include "resource_manager.h"
#include "transform.h"
#include "scene.h"

struct SceneHandles {
    std::shared_ptr<Engine::Transform> cube1Transform;
    std::shared_ptr<Engine::Transform> cameraTransform;
};

static Engine::MeshAsset generateCubeMeshAsset() {
    using namespace Engine;
    MeshAsset mesh;

    mesh.vertices = {
        // -Z (Front)
        Vertex{ glm::vec3(-1.0f, -1.0f, -1.0f),  glm::vec3(0.0f,  0.0f, -1.0f), glm::vec2(0.0f, 0.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 0
        Vertex{ glm::vec3( 1.0f, -1.0f, -1.0f),  glm::vec3(0.0f,  0.0f, -1.0f), glm::vec2(1.0f, 0.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 1
        Vertex{ glm::vec3( 1.0f,  1.0f, -1.0f),  glm::vec3(0.0f,  0.0f, -1.0f), glm::vec2(1.0f, 1.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 2
        Vertex{ glm::vec3(-1.0f,  1.0f, -1.0f),  glm::vec3(0.0f,  0.0f, -1.0f), glm::vec2(0.0f, 1.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 3

        // +Z (Back)
        Vertex{ glm::vec3(-1.0f, -1.0f,  1.0f),  glm::vec3(0.0f, 0.0f,  1.0f), glm::vec2(0.0f, 0.0f), glm::vec4(-1.0f, 0.0f, 0.0f, 1.0f) }, // 4
        Vertex{ glm::vec3( 1.0f, -1.0f,  1.0f),  glm::vec3(0.0f, 0.0f,  1.0f), glm::vec2(1.0f, 0.0f), glm::vec4(-1.0f, 0.0f, 0.0f, 1.0f) }, // 5
        Vertex{ glm::vec3( 1.0f,  1.0f,  1.0f),  glm::vec3(0.0f, 0.0f,  1.0f), glm::vec2(1.0f, 1.0f), glm::vec4(-1.0f, 0.0f, 0.0f, 1.0f) }, // 6
        Vertex{ glm::vec3(-1.0f,  1.0f,  1.0f),  glm::vec3(0.0f, 0.0f,  1.0f), glm::vec2(0.0f, 1.0f), glm::vec4(-1.0f, 0.0f, 0.0f, 1.0f) }, // 7

        // -X (Left)
        Vertex{ glm::vec3(-1.0f, -1.0f,  1.0f),  glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec2(0.0f, 0.0f),  glm::vec4(0.0f, 0.0f, -1.0f, 1.0f) }, // 8
        Vertex{ glm::vec3(-1.0f, -1.0f, -1.0f),  glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec2(1.0f, 0.0f),  glm::vec4(0.0f, 0.0f, -1.0f, 1.0f) }, // 9
        Vertex{ glm::vec3(-1.0f,  1.0f, -1.0f),  glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec2(1.0f, 1.0f),  glm::vec4(0.0f, 0.0f, -1.0f, 1.0f) }, // 10
        Vertex{ glm::vec3(-1.0f,  1.0f,  1.0f),  glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec2(0.0f, 1.0f),  glm::vec4(0.0f, 0.0f, -1.0f, 1.0f) }, // 11

        // +X (Right)
        Vertex{ glm::vec3( 1.0f, -1.0f, -1.0f),  glm::vec3(1.0f,  0.0f,  0.0f), glm::vec2(0.0f, 0.0f),  glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) }, // 12
        Vertex{ glm::vec3( 1.0f, -1.0f,  1.0f),  glm::vec3(1.0f,  0.0f,  0.0f), glm::vec2(1.0f, 0.0f),  glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) }, // 13
        Vertex{ glm::vec3( 1.0f,  1.0f,  1.0f),  glm::vec3(1.0f,  0.0f,  0.0f), glm::vec2(1.0f, 1.0f),  glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) }, // 14
        Vertex{ glm::vec3( 1.0f,  1.0f, -1.0f),  glm::vec3(1.0f,  0.0f,  0.0f), glm::vec2(0.0f, 1.0f),  glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) }, // 15

        // +Y (Top)
        Vertex{ glm::vec3(-1.0f,  1.0f, -1.0f),  glm::vec3(0.0f,  1.0f,  0.0f), glm::vec2(0.0f, 0.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 16
        Vertex{ glm::vec3( 1.0f,  1.0f, -1.0f),  glm::vec3(0.0f,  1.0f,  0.0f), glm::vec2(1.0f, 0.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 17
        Vertex{ glm::vec3( 1.0f,  1.0f,  1.0f),  glm::vec3(0.0f,  1.0f,  0.0f), glm::vec2(1.0f, 1.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 18
        Vertex{ glm::vec3(-1.0f,  1.0f,  1.0f),  glm::vec3(0.0f,  1.0f,  0.0f), glm::vec2(0.0f, 1.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 19

        // -Y (Bottom)
        Vertex{ glm::vec3(-1.0f, -1.0f,  1.0f),  glm::vec3(0.0f, -1.0f,  0.0f), glm::vec2(0.0f, 0.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 20
        Vertex{ glm::vec3( 1.0f, -1.0f,  1.0f),  glm::vec3(0.0f, -1.0f,  0.0f), glm::vec2(1.0f, 0.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 21
        Vertex{ glm::vec3( 1.0f, -1.0f, -1.0f),  glm::vec3(0.0f, -1.0f,  0.0f), glm::vec2(1.0f, 1.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 22
        Vertex{ glm::vec3(-1.0f, -1.0f, -1.0f),  glm::vec3(0.0f, -1.0f,  0.0f), glm::vec2(0.0f, 1.0f),  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }, // 23
    };

    mesh.indices = {
        // Front face
        0, 1, 2,  2, 3, 0,
        // Back face
        4, 5, 6, 6, 7, 4,
        // Left face
        8, 9,10, 10,11, 8,
        // Right face
        12,13,14, 14,15,12,
        // Top face
        16,17,18, 18,19,16,
        // Bottom face
        20,21,22, 22,23,20
    };

    return mesh;
}

static SceneHandles generateBasicScene(Engine::ResourceManager& resources, Engine::Scene& scene) {

    const Engine::MeshHandle cubeMesh = resources.addMesh(generateCubeMeshAsset());
    const Engine::MaterialHandle dummyMaterial = resources.addMaterial(Engine::MaterialAsset{});

    auto& cameraEntity = scene.createEntity(EntityType::NONE);
    std::shared_ptr<Engine::Transform> cameraTransform;
    std::shared_ptr<Engine::Transform> cube1Transform;
    {
        auto cameraComponent    = scene.createComponent<Engine::Camera>(Engine::ProjectionType::Perspective);
        auto transformComponent = scene.createComponent<Engine::Transform>(glm::vec3(0.0f, 4.0f, -7.0f));
        cameraTransform = transformComponent;

        cameraEntity.addComponent(cameraComponent);
        cameraEntity.addComponent(cameraTransform);
    }

    auto& cube1 = scene.createEntity(EntityType::NONE);
    {
        auto meshComponent      = scene.createComponent<Engine::Mesh>(cubeMesh, dummyMaterial, true, true);
        auto transformComponent = scene.createComponent<Engine::Transform>(glm::vec3(0.0f, 2.0f, 0.0f));
        cube1Transform = transformComponent;

        cube1.addComponent(meshComponent);
        cube1.addComponent(transformComponent);
    }

    auto& cube2 = scene.createEntity(EntityType::NONE);
    {
        auto meshComponent      = scene.createComponent<Engine::Mesh>(cubeMesh, dummyMaterial, true, true);
        auto transformComponent = scene.createComponent<Engine::Transform>(glm::vec3(0.0f, -1.0f, 0.0f));
        transformComponent->setScale(glm::vec3(5.0f, 0.5f, 5.0f));

        cube2.addComponent(meshComponent);
        cube2.addComponent(transformComponent);
    }

    return SceneHandles{
        .cube1Transform  = cube1Transform,
        .cameraTransform = cameraTransform,
    };
}

int main() {
    try {
        const std::string rootDir = APP_ROOT_DIR;
        const std::string logFile = rootDir + "/logs/log.log";

        if (!Logger::init(logFile, "ENGINE", LogLevel::TRACE)) return -1;

        printBuildInfo();
        Core::enableGLDebugLogging(true);

        auto& windowManager = WindowManager::get();
        auto& eventManager = EventManager::get();
        auto& statisticTracker = StatisticTracker::get();

        windowManager.createWindow("VKM Engine");
        windowManager.updateMode(WindowMode::WINDOWED);
        windowManager.setFramerate(10000);

        Core::Context glContext;
        glContext.setClearColor({0.1f, 0.1f, 0.1f, 1.0f});
        glContext.setDefaultState();
        glContext.setFaceCulling(false);

        Engine::RenderManager renderer;
        Engine::ResourceManager resources;

        Core::Shader shader("../shaders/basic");

        renderer.setBackend(std::make_unique<Engine::GLBackend>(glContext));
        renderer.addPass(std::make_unique<Engine::GLForwardPass>(shader));

        Engine::Scene scene;

        auto handles = generateBasicScene(resources, scene);

        using clock = std::chrono::steady_clock;
        auto lastStatsPrint = clock::now();

        float cubeAngle = 0.0f;
        float cameraAngle = 0.0f;

        constexpr int viewportWidth  = 1920;
        constexpr int viewportHeight = 1080;

        while (windowManager.beginFrame()) {
            if (!windowManager.updateInput()) break;

            eventManager.executeAsync();

            // Animate: spin the first cube around Y
            cubeAngle += 0.0005f;
            if (handles.cube1Transform) {
                handles.cube1Transform->setRotation(glm::angleAxis(cubeAngle, glm::vec3(1.0f, 1.0f, 0.0f)));
            }

            cameraAngle += 0.00005f;
            if (handles.cameraTransform) {
                handles.cameraTransform->setPosition({10.0f * std::cos(cameraAngle), 2.0f * std::sin(cameraAngle), 10.0f * std::sin(cameraAngle)});
                handles.cameraTransform->setRotation(glm::quatLookAt(handles.cameraTransform->getPosition(), {0.0f, 1.0f, 0.0f}));
            }

            renderer.renderFrame(scene, resources, viewportWidth, viewportHeight);

            if (!windowManager.swapBuffers()) break;

            statisticTracker.update();
            const auto now = clock::now();

            if (now - lastStatsPrint >= std::chrono::milliseconds(500)) {
                const auto& info = statisticTracker.getFrameInfo();
                std::printf("[%lu] FPS: %.2f (%.4fms) | Draws: %u | Entities: %u\n",
                    info.frameIndex,
                    info.frameRateInfo.frameRate,
                    info.frameRateInfo.frameTime,
                    info.renderSystemInfo.drawCalls,
                    info.entitySystemInfo.entityUpdates
                );
                std::fflush(stdout);
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
