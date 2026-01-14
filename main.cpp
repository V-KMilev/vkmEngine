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

// Engine Core
#include "animation_manager.h"
#include "resource_manager.h"
#include "window_manager.h"
#include "event_manager.h"
#include "input_handle.h"
#include "statistics.h"
#include "scene.h"

// Engine Rendering
#include "render_manager.h"

// Backend
#include "gl_backend.h"
#include "gl_forward_pass.h"
#include "gl_aabb_debug_pass.h"
#include "gl_grid_pass.h"
#include "gl_navigation_gizmo_pass.h"

// Tools - Loaders
#include "texture_loaders.h"
#include "material_loaders.h"

// Tools - Generators
#include "mesh_generators.h"
#include "texture_generators.h"
#include "material_generators.h"
#include "light_generators.h"

// Editor
#include "camera_controller.h"

static void generateBasicScene(Engine::ResourceManager& resources, Engine::Scene& scene, Engine::CameraController& cameraController) {
    Engine::MeshHandle cubeMesh          = resources.add(Engine::generateCube());
    Engine::MeshHandle sphereMesh        = resources.add(Engine::generateSphere());

    // Load PBR material from folder (automatic texture detection)
    Engine::MaterialHandle pavingMaterial = Engine::loadMaterialFromFolder(
        "assets/PavingStones118_2K-JPG",
        resources
    );

    auto cameraEntity = scene.createEntity();
    {
        scene.add(cameraEntity, Engine::Camera{Engine::ProjectionType::Perspective});
        scene.add(cameraEntity, Engine::Transform{});
        // scene.add(cameraEntity, Engine::Animation{});
    }
    cameraController.setCameraEntity(cameraEntity);

    // Cube with default material
    auto cube1 = scene.createEntity();
    {
        scene.add(cube1, Engine::Mesh{cubeMesh, pavingMaterial});
        scene.add(cube1, Engine::Transform{glm::vec3(0.0f, 7.0f, 0.0f)});
        scene.add(cube1, Engine::Animation{});
    }

    // Cube with PBR paving stone material
    auto cube2 = scene.createEntity();
    {
        scene.add(cube2, Engine::Mesh{cubeMesh, pavingMaterial});  // Using real textures!
        scene.add(cube2, Engine::Transform{glm::vec3(0.0f, 4.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(5.0f, 0.5f, 5.0f)});
        scene.add(cube2, Engine::Animation{});
    }

    for (int i = 1; i < 100000; i++) {
        auto gridObject = scene.createEntity();
        {
            int gridSize = 316;
            int x = i % gridSize;
            float y = 0.0f;
            int z = i / gridSize;

            float spacing = 2.5f;
            float gridCenterOffset = (gridSize - 1) * spacing * 0.5f;

            if (((x + z) % 2) == 0) {
                scene.add(gridObject, Engine::Mesh{i % 2 == 0 ? sphereMesh : cubeMesh, pavingMaterial});
                scene.add(gridObject, Engine::Animation{});
                scene.add(gridObject, Engine::Transform{
                    glm::vec3(x * spacing - gridCenterOffset, y, z * spacing - gridCenterOffset)
                });
            }
        }
    }

    // Point light (white glow)
    auto pointLight1 = scene.createEntity();
    {
        scene.add(pointLight1, Engine::generatePointLight(
            glm::vec3(1.0f, 1.0f, 1.0f),
            10.0f,
            20.0f
        ));
        scene.add(pointLight1, Engine::Transform{glm::vec3(0.0f, 10.0f, 0.0f)});
    }

    auto pointLight2 = scene.createEntity();
    {
        scene.add(pointLight2, Engine::generatePointLight(
            glm::vec3(1.0f, 1.0f, 1.0f),
            5.0f,
            100.0f
        ));
        scene.add(pointLight2, Engine::Transform{glm::vec3(10.0f, 2.0f, 10.0f)});
    }

    auto pointLight3 = scene.createEntity();
    {
        scene.add(pointLight3, Engine::generatePointLight(
            glm::vec3(1.0f, 1.0f, 1.0f),
            5.0f,
            100.0f
        ));
        scene.add(pointLight3, Engine::Transform{glm::vec3(10.0f, 2.0f, -10.0f)});
    }
}

static void generateAnimations(Engine::Scene& scene) {
    auto& animationStorage = scene.storage<Engine::Animation>();
    for (Engine::EntityId id = 0; id < animationStorage.size(); ++id) {
        if (!animationStorage.has(id)) {
            continue;
        }

        auto& anim = scene.get<Engine::Animation>(Engine::Entity{id});

        if (id == 1) {
            auto& positionTrack = anim.positionTrack;
            auto& rotationTrack = anim.rotationTrack;
            positionTrack.setEasing(Easing::linear);
            rotationTrack.setEasing(Easing::easeInOutSine);

            constexpr float duration = 20.0f;
            constexpr float radius = 25.0f;
            constexpr float height = 10.0f;

            for (int i = 0; i <= static_cast<int>(duration); ++i) {
                float t = float(i) / duration;
                float angle = t * glm::two_pi<float>();

                glm::vec3 pos = {
                    radius * std::cos(angle),
                    glm::abs(height * std::sin(angle)),
                    radius * std::sin(angle)
                };
                glm::vec3 forward = glm::normalize(pos - glm::vec3(0.0f));
                positionTrack.addKeyframe(t * duration, pos);
                rotationTrack.addKeyframe(t * duration, glm::quatLookAt(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            }
        }
        else if (id == 3) {
            auto& rotationTrack = anim.rotationTrack;
            rotationTrack.setEasing(Easing::linear);
            constexpr float duration = 10.0f;
            glm::vec3 axis = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));
            rotationTrack.addKeyframe(0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            rotationTrack.addKeyframe(duration/2, glm::angleAxis(glm::pi<float>(), axis));
            rotationTrack.addKeyframe(duration, glm::angleAxis(glm::two_pi<float>(), axis));
        }
        else if (id >= 2) {
            if (id % 2 == 0) {
                auto& rotationTrack = anim.rotationTrack;
                rotationTrack.setEasing(Easing::linear);
                constexpr float duration = 10.0f;
                glm::vec3 axis = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
                rotationTrack.addKeyframe(0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                rotationTrack.addKeyframe(duration/2, glm::angleAxis(glm::pi<float>(), axis));
                rotationTrack.addKeyframe(duration, glm::angleAxis(glm::two_pi<float>(), axis));
            } else {
                auto& positionTrack = anim.positionTrack;
                positionTrack.setEasing(Easing::easeInOutSine);

                // Grid layout
                constexpr int gridSize = 316;
                constexpr float spacing = 2.5f;
                constexpr float offset = (gridSize - 1) * spacing * 0.5f;
                int x = id % gridSize;
                int z = id / gridSize;

                constexpr float duration = 3.0f;
                float px = x * spacing - offset;
                float pz = z * spacing - offset;

                positionTrack.addKeyframe(0.0f, glm::vec3(px, -3.0f, pz));
                positionTrack.addKeyframe(duration/3, glm::vec3(px, 0.0f, pz));
                positionTrack.addKeyframe(2*duration/3, glm::vec3(px, 1.0f, pz));
                positionTrack.addKeyframe(duration, glm::vec3(px, -3.0f, pz));
            }
        }

        anim.looping = true;
        anim.playing = true;
    }
}

int main() {
    try {
        const std::string rootDir = APP_ROOT_DIR;
        const std::string logFile = rootDir + "/logs/log.log";

        if (!Logger::init(logFile, "ENGINE", LogLevel::TRACE)) {
            return -1;
        }

        printBuildInfo();
        Core::enableGLDebugLogging(true);

        auto& windowManager    = WindowManager::get();
        auto& eventManager     = EventManager::get();
        auto& statisticTracker = StatisticTracker::get();

        windowManager.createWindow("VKM Engine");
        windowManager.updateMode(WindowMode::WINDOWED);
        windowManager.setFramerate(0);

        Engine::ResourceManager resources;
        Engine::AnimationManager animationManager;
        Engine::RenderManager renderManager;

        Core::Shader pbr("shaders/pbr");
        Core::Shader aabbDebug("shaders/aabb_debug");
        Core::Shader gridShader("shaders/grid");
        Core::Shader gizmoShader("shaders/gizmo");

        renderManager.setBackend(std::make_unique<Engine::GLBackend>());
        renderManager.addPass(std::make_unique<Engine::GLForwardPass>(pbr));
        // renderManager.addPass(std::make_unique<Engine::GLAABBDebugPass>(aabbDebug));
        renderManager.addPass(std::make_unique<Engine::GLGridPass>(gridShader));
        renderManager.addPass(std::make_unique<Engine::GLNavigationGizmoPass>(gizmoShader));

        Engine::Scene scene;
        Engine::CameraController cameraController;

        generateBasicScene(resources, scene, cameraController);
        generateAnimations(scene);

        using clock = std::chrono::steady_clock;
        auto lastStatsPrint = clock::now();

        constexpr int viewportWidth  = 1920;
        constexpr int viewportHeight = 1080;

        while (windowManager.beginFrame()) {
            // Frame delta time in seconds
            float deltaTime = statisticTracker.getFrameInfo().frameRateInfo.frameTime / 1000.0f;

            if (!windowManager.updateInput()) break;

            cameraController.update(scene, deltaTime);

            eventManager.executeAsync();
            animationManager.update(scene, deltaTime);
            renderManager.renderFrame(scene, resources, viewportWidth, viewportHeight);

            if (!windowManager.swapBuffers()) break;

            statisticTracker.update();
            const auto now = clock::now();

            if (now - lastStatsPrint >= std::chrono::milliseconds(500)) {
                const auto& info = statisticTracker.getFrameInfo();
                std::printf("[%llu] FPS: %.2f (%.4fms) | Draws: %u | Entities: %u\n",
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
