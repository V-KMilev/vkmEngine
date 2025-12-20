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
#include "entity.h"

#include "mesh_asset.h"
#include "material_asset.h"

#include "render_manager.h"

#include "gl_backend.h"
#include "gl_mesh.h"
#include "gl_material.h"
#include "gl_view.h"
#include "gl_forward_pass.h"

#include "resource_manager.h"
#include "transform.h"
#include "scene.h"
#include "animation.h"
#include "animation_manager.h"

static Engine::MeshAsset generateSphereMeshAsset() {
    using namespace Engine;
    MeshAsset mesh;

    const unsigned int X_SEGMENTS = 32;
    const unsigned int Y_SEGMENTS = 16;
    const float PI = glm::pi<float>();

    // Generate vertices
    for (unsigned int y = 0; y <= Y_SEGMENTS; ++y) {
        for (unsigned int x = 0; x <= X_SEGMENTS; ++x) {
            float xSegment = (float)x / (float)X_SEGMENTS;
            float ySegment = (float)y / (float)Y_SEGMENTS;
            float xPos = std::cos(xSegment * 2.0f * PI) * std::sin(ySegment * PI);
            float yPos = std::cos(ySegment * PI);
            float zPos = std::sin(xSegment * 2.0f * PI) * std::sin(ySegment * PI);

            glm::vec3 position = glm::vec3(xPos, yPos, zPos);
            glm::vec3 normal = glm::normalize(position);
            glm::vec2 texCoords = glm::vec2(xSegment, ySegment);

            // Tangent calculation
            // For a sphere, use derivation along the s/x axis for the tangent
            glm::vec4 tangent;
            float theta = xSegment * 2.0f * PI;
            float phi = ySegment * PI;
            tangent.x = -std::sin(theta) * std::sin(phi);
            tangent.y = 0.0f;
            tangent.z = std::cos(theta) * std::sin(phi);
            tangent.w = 1.0f;
            tangent = glm::normalize(tangent);

            mesh.vertices.push_back(Vertex{
                position,
                normal,
                texCoords,
                tangent
            });
        }
    }

    // Generate indices
    bool oddRow = false;
    for (unsigned int y = 0; y < Y_SEGMENTS; ++y) {
        for (unsigned int x = 0; x < X_SEGMENTS; ++x) {
            unsigned int i0 = y * (X_SEGMENTS + 1) + x;
            unsigned int i1 = (y + 1) * (X_SEGMENTS + 1) + x;
            unsigned int i2 = (y + 1) * (X_SEGMENTS + 1) + (x + 1);
            unsigned int i3 = y * (X_SEGMENTS + 1) + (x + 1);

            // Two triangles per quad
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);

            mesh.indices.push_back(i2);
            mesh.indices.push_back(i3);
            mesh.indices.push_back(i0);
        }
    }

    return mesh;
}

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
    Engine::MeshHandle cubeMesh          = resources.add(generateCubeMeshAsset());
    Engine::MeshHandle sphereMesh        = resources.add(generateSphereMeshAsset());
    Engine::MaterialHandle dummyMaterial = resources.add(Engine::MaterialAsset{});

    auto cameraEntity = scene.createEntity();
    {
        scene.add(cameraEntity, Engine::Camera{Engine::ProjectionType::Perspective});
        scene.add(cameraEntity, Engine::Transform{});
        scene.add(cameraEntity, Engine::Animation{});
    }

    auto cube1 = scene.createEntity();
    {
        scene.add(cube1, Engine::Mesh{cubeMesh, dummyMaterial});
        scene.add(cube1, Engine::Transform{glm::vec3(0.0f, 5.0f, 0.0f)});
        scene.add(cube1, Engine::Animation{});
    }

    auto cube2 = scene.createEntity();
    {
        scene.add(cube2, Engine::Mesh{cubeMesh, dummyMaterial});
        scene.add(cube2, Engine::Transform{glm::vec3(0.0f, 2.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(5.0f, 0.5f, 5.0f)});
        scene.add(cube2, Engine::Animation{});
    }

    for (int i = 1; i < 10000; i++) {
        auto gridObject = scene.createEntity();
        {
            int gridSize = 100;
            int x = i % gridSize;
            float y = -3.0f;
            int z = i / gridSize;

            float spacing = 2.5f;
            float gridCenterOffset = (gridSize - 1) * spacing * 0.5f;

            if (((x + z) % 2) == 0) {
                scene.add(gridObject, Engine::Mesh{i % 2 == 0 ? sphereMesh : cubeMesh, dummyMaterial});
                scene.add(gridObject, Engine::Animation{});
                scene.add(gridObject, Engine::Transform{
                    glm::vec3(x * spacing - gridCenterOffset, y, z * spacing - gridCenterOffset)
                });
            }
        }
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
                glm::vec3 forward = glm::normalize(pos - glm::vec3(0));
                positionTrack.addKeyframe(t * duration, pos);
                rotationTrack.addKeyframe(t * duration, glm::quatLookAt(forward, glm::vec3(0,1,0)));
            }
        }
        else if (id == 3) {
            auto& rotationTrack = anim.rotationTrack;
            rotationTrack.setEasing(Easing::linear);
            constexpr float duration = 10.0f;
            glm::vec3 axis = glm::normalize(glm::vec3(0, 1, 0));
            rotationTrack.addKeyframe(0.0f, glm::quat(1, 0, 0, 0));
            rotationTrack.addKeyframe(duration/2, glm::angleAxis(glm::pi<float>(), axis));
            rotationTrack.addKeyframe(duration, glm::angleAxis(glm::two_pi<float>(), axis));
        }
        else if (id >= 2) {
            if (id % 2 == 0) {
                auto& rotationTrack = anim.rotationTrack;
                rotationTrack.setEasing(Easing::linear);
                constexpr float duration = 10.0f;
                glm::vec3 axis = glm::normalize(glm::vec3(1, 1, 0));
                rotationTrack.addKeyframe(0.0f, glm::quat(1, 0, 0, 0));
                rotationTrack.addKeyframe(duration/2, glm::angleAxis(glm::pi<float>(), axis));
                rotationTrack.addKeyframe(duration, glm::angleAxis(glm::two_pi<float>(), axis));
            } else {
                auto& positionTrack = anim.positionTrack;
                positionTrack.setEasing(Easing::easeInOutSine);

                // Grid layout
                constexpr int gridSize = 100;
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
        auto& animationManager = Engine::AnimationManager::get();

        windowManager.createWindow("VKM Engine");
        windowManager.updateMode(WindowMode::WINDOWED);
        windowManager.setFramerate(0);

        Engine::RenderManager renderManager;
        Engine::ResourceManager resources;

        Core::Shader shader("../shaders/pbr");

        renderManager.setBackend(std::make_unique<Engine::GLBackend>());
        renderManager.addPass(std::make_unique<Engine::GLForwardPass>(shader));

        Engine::Scene scene;

        generateBasicScene(resources, scene);
        generateAnimations(scene);

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
