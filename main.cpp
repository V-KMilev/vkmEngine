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
#include "visibility.h"

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
#define GRID_SIZE 1000
#define SPACING 3 + 1
#define HALF_GRID_SIZE GRID_SIZE / 2

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
    }
    cameraController.setCameraEntity(cameraEntity);

    auto cube1 = scene.createEntity();
    {
        scene.add(cube1, Engine::Mesh{cubeMesh, pavingMaterial});
        scene.add(cube1, Engine::Transform{glm::vec3(0.0f, 7.0f, 0.0f)});
        scene.add(cube1, Engine::Animation{});
    }

    auto cube2 = scene.createEntity();
    {
        scene.add(cube2, Engine::Mesh{cubeMesh, pavingMaterial});
        scene.add(cube2, Engine::Transform{glm::vec3(0.0f, 4.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(5.0f, 0.5f, 5.0f)});
        scene.add(cube2, Engine::Animation{});
    }

    for (int x = -HALF_GRID_SIZE; x < HALF_GRID_SIZE; ++x) {
        for (int z = -HALF_GRID_SIZE; z < HALF_GRID_SIZE; ++z) {
            glm::vec3 position = glm::vec3(
                static_cast<float>(x * SPACING),
                0.0f,
                static_cast<float>(z * SPACING)
            );

            auto object = scene.createEntity();
            scene.add(object, Engine::Mesh{(x + z) % 2 == 0 ? sphereMesh : cubeMesh, pavingMaterial});
            scene.add(object, Engine::Animation{});
            scene.add(object, Engine::Transform{position});
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

        if (id == 3) {
            auto& rotationTrack = anim.rotationTrack;
            rotationTrack.setEasing(Easing::linear);
            constexpr float duration = 10.0f;
            glm::vec3 axis = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));
            rotationTrack.addKeyframe(0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            rotationTrack.addKeyframe(duration/2, glm::angleAxis(glm::pi<float>(), axis));
            rotationTrack.addKeyframe(duration, glm::angleAxis(glm::two_pi<float>(), axis));
        }
        else if (id > 3) {
            constexpr float duration = 5.0f;
            // Grid entities start at id 3 (after camera, cube1, cube2)
            // id == 3 has special handling above, so grid logic starts at id > 3
            // The grid is created with z as inner loop, so z changes faster
            int gridIndex = id - 3;
            int zIndex = gridIndex % GRID_SIZE;
            int xIndex = gridIndex / GRID_SIZE;
            int px = (xIndex - HALF_GRID_SIZE) * SPACING;
            int pz = (zIndex - HALF_GRID_SIZE) * SPACING;

            if (id % 2 == 0) {
                auto& positionTrack = anim.positionTrack;
                positionTrack.setEasing(Easing::easeInOutSine);

                auto& rotationTrack = anim.rotationTrack;
                rotationTrack.setEasing(Easing::linear);

                positionTrack.addKeyframe(0.0f, glm::vec3(px, 0.0f, pz));
                positionTrack.addKeyframe(duration, glm::vec3(px, 0.0f, pz));

                glm::vec3 axis = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
                rotationTrack.addKeyframe(0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                rotationTrack.addKeyframe(duration/2, glm::angleAxis(glm::pi<float>(), axis));
                rotationTrack.addKeyframe(duration, glm::angleAxis(glm::two_pi<float>(), axis));
            } else {
                auto& positionTrack = anim.positionTrack;
                positionTrack.setEasing(Easing::easeInOutSine);

                positionTrack.addKeyframe(0.0f, glm::vec3(px, 0.0f, pz));
                positionTrack.addKeyframe(duration/3, glm::vec3(px, 1.0f, pz));
                positionTrack.addKeyframe(2*duration/3, glm::vec3(px, -1.0f, pz));
                positionTrack.addKeyframe(duration, glm::vec3(px, 0.0f, pz));
            }
        }

        anim.looping = true;
        anim.playing = true;

        // Update cached duration after setting up tracks
        anim.updateDuration();
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

            auto visibility = Engine::buildVisibility(scene, resources);

            animationManager.update(scene, visibility, deltaTime);
            renderManager.renderFrame(scene, resources, visibility, viewportWidth, viewportHeight);

            if (!windowManager.swapBuffers()) break;

            statisticTracker.update();
            const auto now = clock::now();

            if (now - lastStatsPrint >= std::chrono::milliseconds(500)) {
                const auto& info = statisticTracker.getFrameInfo();
                std::printf("[%llu] FPS: %.2f (%.4fms) | Draws: %u | Entities: %zu/%u\n",
                    info.frameIndex,
                    info.frameRateInfo.frameRate,
                    info.frameRateInfo.frameTime,
                    info.renderSystemInfo.drawCalls,
                    visibility.entities.size(),
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
