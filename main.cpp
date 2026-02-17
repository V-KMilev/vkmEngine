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

struct BenchmarkConfig {
    int gridSize = 200;           // Objects per axis (total = gridSize^2)
    float spacing = 3.0f;         // Space between objects

    // Distance layers for culling stress
    int nearLayerCount = 500;     // Objects close to origin
    int midLayerCount = 1000;     // Objects at medium distance
    int farLayerCount = 2000;     // Objects at far distance

    // Animation stress
    float animationRatio = 0.3f;  // Ratio of objects with animations

    // Lights
    int pointLightCount = 8;      // Number of point lights
    int spotLightCount = 4;       // Number of spot lights

    // Mesh variety (more unique meshes = more potential draw calls)
    bool useMeshVariety = true;   // Use different mesh types
    bool useSphereDetail = true;  // Use high-detail spheres
};

static void generateBenchmarkScene(
    Engine::ResourceManager& resources,
    Engine::Scene& scene,
    Engine::CameraController& cameraController,
    const BenchmarkConfig& config = BenchmarkConfig{}
) {
    // Create mesh variety
    Engine::MeshHandle cubeMesh = resources.add(Engine::generateCube());
    Engine::MeshHandle sphereMesh = config.useSphereDetail
        ? resources.add(Engine::generateSphere(48, 24))  // High detail
        : resources.add(Engine::generateSphere(32, 16));
    Engine::MeshHandle coneMesh = resources.add(Engine::generateCone(0.5f, 1.0f, 24));
    Engine::MeshHandle pyramidMesh = resources.add(Engine::generatePyramid(2.0f, 2.0f));
    Engine::MeshHandle planeMesh = resources.add(Engine::generatePlane(2.0f, 2.0f, 4, 4));

    std::vector<Engine::MeshHandle> meshes = {cubeMesh, sphereMesh};
    if (config.useMeshVariety) {
        meshes.push_back(coneMesh);
        meshes.push_back(pyramidMesh);
    }

    // Load materials
    Engine::MaterialHandle material1 = Engine::loadMaterialFromFolder("../assets/PavingStones118_2K-JPG", resources);
    Engine::MaterialHandle material2 = Engine::loadMaterialFromFolder("../assets/PavingStones115A_2K-JPG", resources);
    std::vector<Engine::MaterialHandle> materials = {material1, material2};

    // Camera
    auto cameraEntity = scene.createEntity();
    {
        scene.add(cameraEntity, Engine::Camera{Engine::ProjectionType::Perspective});
        scene.add(cameraEntity, Engine::Transform{glm::vec3(0.0f, 10.0f, 30.0f)});
    }
    cameraController.setCameraEntity(cameraEntity);

    int entityIndex = 0;
    auto shouldAnimate = [&]() {
        return (entityIndex++ % static_cast<int>(1.0f / config.animationRatio)) == 0;
    };

    const int halfGrid = config.gridSize / 2;
    for (int x = -halfGrid; x < halfGrid; ++x) {
        for (int z = -halfGrid; z < halfGrid; ++z) {
            glm::vec3 position(
                static_cast<float>(x) * config.spacing,
                0.0f,
                static_cast<float>(z) * config.spacing
            );

            auto entity = scene.createEntity();
            scene.add(entity, Engine::Mesh{
                meshes[(x + z + halfGrid * 2) % meshes.size()],
                materials[(x + z) % materials.size()]
            });
            scene.add(entity, Engine::Transform{position});

            if (shouldAnimate()) {
                scene.add(entity, Engine::Animation{});
            }
        }
    }

    for (int i = 0; i < config.nearLayerCount; ++i) {
        float angle = (static_cast<float>(i) / config.nearLayerCount) * glm::two_pi<float>() * 3.0f;
        float radius = 5.0f + (i % 20) * 0.5f;
        float height = 0.5f + static_cast<float>(i % 10) * 0.3f;

        glm::vec3 position(
            std::cos(angle) * radius,
            height,
            std::sin(angle) * radius
        );

        auto entity = scene.createEntity();
        scene.add(entity, Engine::Mesh{
            meshes[i % meshes.size()],
            materials[i % materials.size()]
        });
        scene.add(entity, Engine::Transform{position, glm::vec3(0.0f), glm::vec3(0.3f)});

        if (shouldAnimate()) {
            scene.add(entity, Engine::Animation{});
        }
    }

    for (int i = 0; i < config.midLayerCount; ++i) {
        float angle = (static_cast<float>(i) / config.midLayerCount) * glm::two_pi<float>() * 5.0f;
        float radius = 80.0f + (i % 50) * 2.0f;
        float height = static_cast<float>(i % 15) * 1.0f;

        glm::vec3 position(
            std::cos(angle) * radius,
            height,
            std::sin(angle) * radius
        );

        auto entity = scene.createEntity();
        scene.add(entity, Engine::Mesh{
            meshes[i % meshes.size()],
            materials[i % materials.size()]
        });
        scene.add(entity, Engine::Transform{position});

        if (shouldAnimate()) {
            scene.add(entity, Engine::Animation{});
        }
    }

    for (int i = 0; i < config.farLayerCount; ++i) {
        float angle = (static_cast<float>(i) / config.farLayerCount) * glm::two_pi<float>() * 8.0f;
        float radius = 200.0f + (i % 100) * 3.0f;
        float height = static_cast<float>(i % 20) * 2.0f;

        glm::vec3 position(
            std::cos(angle) * radius,
            height,
            std::sin(angle) * radius
        );

        auto entity = scene.createEntity();
        scene.add(entity, Engine::Mesh{
            meshes[i % meshes.size()],
            materials[i % materials.size()]
        });
        // Larger scale so they're still visible at distance
        scene.add(entity, Engine::Transform{position, glm::vec3(0.0f), glm::vec3(2.0f)});

        if (shouldAnimate()) {
            scene.add(entity, Engine::Animation{});
        }
    }

    for (int tower = 0; tower < 8; ++tower) {
        float towerAngle = (static_cast<float>(tower) / 8.0f) * glm::two_pi<float>();
        float towerRadius = 50.0f;
        glm::vec3 towerBase(
            std::cos(towerAngle) * towerRadius,
            0.0f,
            std::sin(towerAngle) * towerRadius
        );

        for (int y = 0; y < 30; ++y) {
            auto entity = scene.createEntity();
            scene.add(entity, Engine::Mesh{
                meshes[y % meshes.size()],
                materials[tower % materials.size()]
            });
            scene.add(entity, Engine::Transform{
                towerBase + glm::vec3(0.0f, static_cast<float>(y) * 2.5f, 0.0f)
            });

            if (y % 5 == 0) {
                scene.add(entity, Engine::Animation{});
            }
        }
    }

    // Directional light (sun)
    auto sunLight = scene.createEntity();
    {
        scene.add(sunLight, Engine::generateDirectionalLight(
            glm::vec3(1.0f, 0.95f, 0.9f), 2.0f
        ));
        scene.add(sunLight, Engine::Transform{
            glm::vec3(0.0f, 100.0f, 0.0f),
            glm::vec3(-0.5f, 0.2f, 0.0f)
        });
    }

    // Point lights in a ring
    for (int i = 0; i < config.pointLightCount; ++i) {
        float angle = (static_cast<float>(i) / config.pointLightCount) * glm::two_pi<float>();
        float radius = 30.0f;

        glm::vec3 color(
            0.5f + 0.5f * std::sin(angle),
            0.5f + 0.5f * std::cos(angle),
            0.5f + 0.5f * std::sin(angle + glm::pi<float>())
        );

        auto light = scene.createEntity();
        scene.add(light, Engine::generatePointLight(color, 15.0f, 40.0f));
        scene.add(light, Engine::Transform{glm::vec3(
            std::cos(angle) * radius,
            8.0f,
            std::sin(angle) * radius
        )});
    }

    // Spot lights pointing inward
    for (int i = 0; i < config.spotLightCount; ++i) {
        float angle = (static_cast<float>(i) / config.spotLightCount) * glm::two_pi<float>();
        float radius = 60.0f;

        auto light = scene.createEntity();
        scene.add(light, Engine::generateSpotLight(
            glm::vec3(1.0f, 1.0f, 1.0f),
            20.0f, 50.0f, 0.3f, 0.6f
        ));
        scene.add(light, Engine::Transform{
            glm::vec3(std::cos(angle) * radius, 15.0f, std::sin(angle) * radius),
            glm::vec3(0.0f, angle + glm::pi<float>(), -0.3f)
        });
    }

    LOG_INFO("Benchmark scene created:");
    LOG_INFO("  Grid: %dx%d = %d entities", config.gridSize, config.gridSize, config.gridSize * config.gridSize);
    LOG_INFO("  Near layer: %d entities", config.nearLayerCount);
    LOG_INFO("  Mid layer: %d entities", config.midLayerCount);
    LOG_INFO("  Far layer: %d entities", config.farLayerCount);
    LOG_INFO("  Towers: 8 x 30 = 240 entities");
    LOG_INFO("  Lights: 1 directional + %d point + %d spot", config.pointLightCount, config.spotLightCount);
    int total = config.gridSize * config.gridSize + config.nearLayerCount +
                config.midLayerCount + config.farLayerCount + 240;
    LOG_INFO("  Total mesh entities: %d", total);
}

static void generateBenchmarkAnimations(Engine::Scene& scene) {
    auto& animationStorage = scene.storage<Engine::Animation>();
    const auto& transformStorage = scene.storage<Engine::Transform>();

    for (Engine::EntityId id = 0; id < animationStorage.size(); ++id) {
        if (!animationStorage.has(id)) continue;
        if (!transformStorage.has(id)) continue;

        auto& anim = animationStorage.get(id);
        const auto& transform = transformStorage.get(id);

        // Varied animation types based on entity id
        int animType = id % 4;
        float duration = 3.0f + (id % 5) * 0.5f;

        if (animType == 0) {
            // Rotation animation
            auto& rotationTrack = anim.rotationTrack;
            rotationTrack.setEasing(Easing::linear);
            glm::vec3 axis = glm::normalize(glm::vec3(
                (id % 3 == 0) ? 1.0f : 0.0f,
                (id % 3 == 1) ? 1.0f : 0.0f,
                (id % 3 == 2) ? 1.0f : 0.0f
            ));
            if (glm::length(axis) < 0.1f) axis = glm::vec3(0.0f, 1.0f, 0.0f);
            rotationTrack.addKeyframe(0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            rotationTrack.addKeyframe(duration / 2, glm::angleAxis(glm::pi<float>(), axis));
            rotationTrack.addKeyframe(duration, glm::angleAxis(glm::two_pi<float>(), axis));
        }
        else if (animType == 1) {
            // Vertical bobbing
            auto& positionTrack = anim.positionTrack;
            positionTrack.setEasing(Easing::easeInOutSine);
            glm::vec3 basePos = transform.position;
            positionTrack.addKeyframe(0.0f, basePos);
            positionTrack.addKeyframe(duration / 2, basePos + glm::vec3(0.0f, 1.5f, 0.0f));
            positionTrack.addKeyframe(duration, basePos);
        }
        else if (animType == 2) {
            // Scale pulsing
            auto& scaleTrack = anim.scaleTrack;
            scaleTrack.setEasing(Easing::easeInOutSine);
            glm::vec3 baseScale = transform.scale;
            scaleTrack.addKeyframe(0.0f, baseScale);
            scaleTrack.addKeyframe(duration / 2, baseScale * 1.3f);
            scaleTrack.addKeyframe(duration, baseScale);
        }
        else {
            // Combined rotation + position
            auto& rotationTrack = anim.rotationTrack;
            rotationTrack.setEasing(Easing::linear);
            glm::vec3 axis(0.0f, 1.0f, 0.0f);
            rotationTrack.addKeyframe(0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            rotationTrack.addKeyframe(duration, glm::angleAxis(glm::two_pi<float>(), axis));

            auto& positionTrack = anim.positionTrack;
            positionTrack.setEasing(Easing::easeInOutSine);
            glm::vec3 basePos = transform.position;
            positionTrack.addKeyframe(0.0f, basePos);
            positionTrack.addKeyframe(duration / 3, basePos + glm::vec3(0.5f, 0.0f, 0.0f));
            positionTrack.addKeyframe(2 * duration / 3, basePos + glm::vec3(-0.5f, 0.0f, 0.0f));
            positionTrack.addKeyframe(duration, basePos);
        }

        anim.looping = true;
        anim.playing = true;
        anim.updateDuration();
    }
}

static void printStats(const Engine::Visibility& visibility) {
    const auto& statisticTracker = StatisticTracker::get();

    static auto lastStatsPrint = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();

    if (now - lastStatsPrint >= std::chrono::milliseconds(500)) {
        const auto& info = statisticTracker.getFrameInfo();
        std::printf("[%lu] FPS: %.2f (%.4fms) | Draws: %u | Entities: %zu/%u\n",
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

int main() {
    try {
        const std::string rootDir = APP_ROOT_DIR;
        const std::string logFile = rootDir + "/logs/log.log";

        if (!Logger::init(logFile, "ENGINE", LogLevel::TRACE)) {
            return -1;
        }

        printBuildInfo();
        Core::enableGLDebugLogging(true);

        auto& windowManager    = Engine::WindowManager::get();
        auto& eventManager     = EventManager::get();
        auto& statisticTracker = StatisticTracker::get();

        windowManager.createWindow("VKM Engine");
        windowManager.updateMode(Engine::WindowMode::WINDOWED);
        windowManager.setFramerate(0);

        Engine::ResourceManager resources;
        Engine::AnimationManager animationManager;
        Engine::RenderManager renderManager;

        Core::Shader pbr("../shaders/pbr");
        Core::Shader aabbDebug("../shaders/aabb_debug");
        Core::Shader gridShader("../shaders/grid");
        Core::Shader gizmoShader("../shaders/gizmo");

        renderManager.setBackend(std::make_unique<Engine::GLBackend>());
        renderManager.addPass(std::make_unique<Engine::GLForwardPass>(pbr));
        // renderManager.addPass(std::make_unique<Engine::GLAABBDebugPass>(aabbDebug));
        renderManager.addPass(std::make_unique<Engine::GLGridPass>(gridShader));
        renderManager.addPass(std::make_unique<Engine::GLNavigationGizmoPass>(gizmoShader));

        Engine::Scene scene;
        Engine::CameraController cameraController;

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

        generateBenchmarkScene(resources, scene, cameraController, config);
        generateBenchmarkAnimations(scene);

        while (windowManager.beginFrame()) {
            size_t viewportWidth  = windowManager.getWidth();
            size_t viewportHeight = windowManager.getHeight();
            float deltaTime       = statisticTracker.getFrameInfo().frameRateInfo.frameTime / 1000.0f;

            if (!windowManager.updateInput()) break;

            cameraController.update(scene, deltaTime);

            eventManager.executeAsync();

            auto visibility = Engine::buildVisibility(scene, resources, viewportWidth, viewportHeight);

            animationManager.update(scene, visibility, deltaTime);

            renderManager.renderFrame(scene, resources, visibility, viewportWidth, viewportHeight);

            if (!windowManager.swapBuffers()) break;

            statisticTracker.update();
            printStats(visibility);
        }
    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
    } catch (...) {
        LOG_FATAL("Unknown exception");
    }

    LOG_INFO("Shutdown successfully!");
    return 0;
}
