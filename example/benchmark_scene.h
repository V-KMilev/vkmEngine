#pragma once

#include <cstdint>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "logger.h"

#include "core/engine.h"
#include "ecs/components.h"
#include "ecs/hierarchy_utils.h"

#include "loader/material_loaders.h"
#include "generator/mesh_generators.h"
#include "generator/material_generators.h"
#include "generator/light_generators.h"

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

static Engine::Entity generateBenchmarkScene(
    Engine::Engine& engine,
    const BenchmarkConfig& config = BenchmarkConfig{}
) {
    auto& scene     = engine.getScene();
    auto& resources = engine.getResources();

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

    // Hierarchy test: orbital groups (center + satellite children)
    // Satellites use local-space offsets; world position comes from hierarchy
    constexpr int orbitalGroupCount = 4;
    constexpr int satellitesPerGroup = 6;
    constexpr float orbitalRadius = 40.0f;
    constexpr float satelliteDistance = 4.0f;

    for (int g = 0; g < orbitalGroupCount; ++g) {
        float groupAngle = (static_cast<float>(g) / orbitalGroupCount) * glm::two_pi<float>();
        glm::vec3 groupPos(
            std::cos(groupAngle) * orbitalRadius,
            5.0f,
            std::sin(groupAngle) * orbitalRadius
        );

        // Center entity (parent)
        auto center = scene.createEntity();
        scene.add(center, Engine::Mesh{sphereMesh, materials[g % materials.size()]});
        scene.add(center, Engine::Transform{groupPos, glm::quat(1, 0, 0, 0), glm::vec3(1.5f)});
        scene.add(center, Engine::Animation{});

        // Satellite entities (children) — local positions offset from center
        for (int s = 0; s < satellitesPerGroup; ++s) {
            float satAngle = (static_cast<float>(s) / satellitesPerGroup) * glm::two_pi<float>();
            glm::vec3 localPos(
                std::cos(satAngle) * satelliteDistance,
                0.0f,
                std::sin(satAngle) * satelliteDistance
            );

            auto satellite = scene.createEntity();
            scene.add(satellite, Engine::Mesh{cubeMesh, materials[(g + s) % materials.size()]});
            scene.add(satellite, Engine::Transform{localPos, glm::quat(1, 0, 0, 0), glm::vec3(0.5f)});
            Engine::HierarchyUtils::setParent(scene, satellite.getID(), center.getID());
        }
    }

    // Hierarchy test: stacked tower (each level is child of the one below)
    // Tests multi-level hierarchy (depth ~10) for world matrix chain
    for (int tower = 0; tower < 2; ++tower) {
        float tAngle = (tower == 0) ? 0.0f : glm::pi<float>();
        glm::vec3 towerBase(
            std::cos(tAngle) * 25.0f,
            0.0f,
            std::sin(tAngle) * 25.0f
        );

        auto prevLevel = scene.createEntity();
        scene.add(prevLevel, Engine::Mesh{cubeMesh, materials[tower % materials.size()]});
        scene.add(prevLevel, Engine::Transform{towerBase, glm::quat(1, 0, 0, 0), glm::vec3(2.0f)});

        for (int level = 1; level < 10; ++level) {
            auto levelEntity = scene.createEntity();
            scene.add(levelEntity, Engine::Mesh{
                meshes[level % meshes.size()],
                materials[level % materials.size()]
            });
            // Local Y offset relative to parent — world position accumulates through chain
            scene.add(levelEntity, Engine::Transform{
                glm::vec3(0.0f, 2.5f, 0.0f),
                glm::quat(1, 0, 0, 0),
                glm::vec3(0.95f)  // Slight taper
            });
            Engine::HierarchyUtils::setParent(scene, levelEntity.getID(), prevLevel.getID());
            prevLevel = levelEntity;
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

    int hierarchyEntities = orbitalGroupCount * (1 + satellitesPerGroup) + 2 * 10;
    LOG_INFO("Benchmark scene created:");
    LOG_INFO("  Grid: %dx%d = %d entities", config.gridSize, config.gridSize, config.gridSize * config.gridSize);
    LOG_INFO("  Near layer: %d entities", config.nearLayerCount);
    LOG_INFO("  Mid layer: %d entities", config.midLayerCount);
    LOG_INFO("  Far layer: %d entities", config.farLayerCount);
    LOG_INFO("  Towers: 8 x 30 = 240 entities");
    LOG_INFO("  Hierarchy: %d entities (%d orbital groups + 2 stacked towers)", hierarchyEntities, orbitalGroupCount);
    LOG_INFO("  Lights: 1 directional + %d point + %d spot", config.pointLightCount, config.spotLightCount);
    int total = config.gridSize * config.gridSize + config.nearLayerCount +
                config.midLayerCount + config.farLayerCount + 240 + hierarchyEntities;
    LOG_INFO("  Total mesh entities: %d", total);

    return cameraEntity;
}

static void generateBenchmarkAnimations(Engine::Scene& scene) {
    scene.forEach<Engine::Animation>([&](Engine::EntityId id, Engine::Animation& anim) {
        if (!scene.has<Engine::Transform>(id)) return;
        const auto& transform = scene.get<Engine::Transform>(id);

        // Varied animation types based on entity sparse index
        uint32_t idx = id.index;
        int animType = idx % 4;
        float duration = 3.0f + (idx % 5) * 0.5f;

        if (animType == 0) {
            // Rotation animation
            auto& rotationTrack = anim.rotationTrack;
            rotationTrack.setEasing(Easing::linear);
            glm::vec3 axis = glm::normalize(glm::vec3(
                (idx % 3 == 0) ? 1.0f : 0.0f,
                (idx % 3 == 1) ? 1.0f : 0.0f,
                (idx % 3 == 2) ? 1.0f : 0.0f
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
    });
}
