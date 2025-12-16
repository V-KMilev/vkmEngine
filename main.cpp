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
#include "animation.h"
#include "animation_manager.h"

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

static void generateBasicScene(Engine::ResourceManager& resources, Engine::Scene& scene) {

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

    for (int i = 1; i < 10000; i++) {
        auto& cube = scene.createEntity(EntityType::NONE);
        {
            auto meshComponent = scene.createComponent<Engine::Mesh>(cubeMesh, dummyMaterial, true, true);

            // Arrange the cubes in a grid for a checkers pattern
            int gridSize = 100;
            int x = i % gridSize;
            float y = 2.0f;
            int z = i / gridSize;

            float spacing = 2.5f;

            // Only place cubes for checker pattern (i.e., only on "black" squares)
            if (((x + z) % 2) == 0) {
                auto transformComponent = scene.createComponent<Engine::Transform>(
                    glm::vec3(x * spacing, y, z * spacing)
                );
                cube.addComponent(transformComponent);
            }

            cube.addComponent(meshComponent);
        }
    }
}

int main() {
    try {
        const std::string rootDir = APP_ROOT_DIR;
        const std::string logFile = rootDir + "/logs/log.log";

        if (!Logger::init(logFile, "ENGINE", LogLevel::TRACE)) return -1;

        printBuildInfo();
        Core::enableGLDebugLogging(true);

        auto& windowManager    = WindowManager::get();
        auto& eventManager     = EventManager::get();
        auto& statisticTracker = StatisticTracker::get();
        auto& animationManager = Engine::AnimationManager::get();

        windowManager.createWindow("VKM Engine");
        windowManager.updateMode(WindowMode::WINDOWED);
        windowManager.setFramerate(0);

        Core::Context glContext;
        glContext.setClearColor({0.1f, 0.1f, 0.1f, 1.0f});
        glContext.setDefaultState();
        glContext.setFaceCulling(false);

        Engine::RenderManager renderManager;
        Engine::ResourceManager resources;

        Core::Shader shader("../shaders/basic");

        renderManager.setBackend(std::make_unique<Engine::GLBackend>(glContext));
        renderManager.addPass(std::make_unique<Engine::GLForwardPass>(shader));

        Engine::Scene scene;

        generateBasicScene(resources, scene);

        for (auto& entity : scene.getEntities()) {
            if (entity.getID() == 2 || entity.getID() > 3 ) {
                // Animate cube1: rotating around Y axis
                auto cube1Animation = scene.createComponent<Engine::Animation>();

                // Create a rotation animation that loops
                auto& rotationTrack = cube1Animation->getRotationTrack();
                rotationTrack.setEasing(Easing::linear);

                const float duration = 10.0f;
                rotationTrack.addKeyframe(0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                rotationTrack.addKeyframe(duration / 2.0f, glm::angleAxis(glm::two_pi<float>() / 2, glm::vec3(1.0f, 1.0f, 0.0f)));
                rotationTrack.addKeyframe(duration, glm::angleAxis(glm::two_pi<float>(), glm::vec3(1.0f, 1.0f, 0.0f)));

                cube1Animation->setLooping(true);
                cube1Animation->play();
                entity.addComponent(cube1Animation);
            } else if (entity.getID() == 1) {
                 auto cameraAnimation = scene.createComponent<Engine::Animation>();

                 auto& positionTrack = cameraAnimation->getPositionTrack();
                 positionTrack.setEasing(Easing::linear);

                 auto& rotationTrack = cameraAnimation->getRotationTrack();
                 rotationTrack.setEasing(Easing::easeInOutSine);
 
                 const float orbitDuration = 30.0f;
                 const float radius = 10.0f;
                 const float height = 2.0f;
 
                 for (int i = 0; i <= int(orbitDuration); ++i) {
                    float t = static_cast<float>(i) / int(orbitDuration);
                    float angle = t * glm::two_pi<float>();

                    glm::vec3 position = {
                        radius * std::cos(angle),
                        height * std::sin(angle),
                        radius * std::sin(angle)
                    };
                    positionTrack.addKeyframe(t * orbitDuration, position);
                    rotationTrack.addKeyframe(t * orbitDuration, glm::quatLookAt(position, glm::vec3(0.0f, 1.0f, 0.0f)));
                 }

                 cameraAnimation->setLooping(true);
                 cameraAnimation->play();
                 entity.addComponent(cameraAnimation);
            }

        }

        using clock = std::chrono::steady_clock;
        auto lastStatsPrint = clock::now();

        constexpr int viewportWidth  = 1920;
        constexpr int viewportHeight = 1080;

        while (windowManager.beginFrame()) {
            if (!windowManager.updateInput()) break;

            eventManager.executeAsync();

            // Update animations (convert frameTime from milliseconds to seconds)
            animationManager.update(scene, statisticTracker.getFrameInfo().frameRateInfo.frameTime / 1000.0f);

            renderManager.renderFrame(scene, resources, viewportWidth, viewportHeight);

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
