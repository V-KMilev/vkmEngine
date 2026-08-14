#include "generator/default_scene.h"

#include <utility>

#include "resource/resource_manager.h"

#include "ecs/scene.h"
#include "ecs/component/camera.h"
#include "ecs/component/mesh.h"
#include "ecs/component/name.h"
#include "ecs/component/transform.h"

#include "generator/light_generators.h"
#include "generator/material_generators.h"
#include "generator/mesh_generators.h"

namespace Engine {

Entity buildDefaultScene(Scene& scene, ResourceManager& resources) {
    const Entity camera = scene.createEntity();
    Transform cameraTransform;
    cameraTransform.position = {0.0f, 2.0f, 6.0f};
    scene.add(camera, cameraTransform);
    scene.add(camera, Camera{});
    scene.add(camera, makeName("Camera"));

    const Entity sun = scene.createEntity();
    Transform sunTransform;
    sunTransform.position = {0.0f, 8.0f, 0.0f};
    scene.add(sun, sunTransform);
    scene.add(sun, generateDirectionalLight());
    scene.add(sun, makeName("Sun"));

    const Entity cube = scene.createEntity();
    scene.add(cube, Transform{});
    scene.add(cube, makeName("Cube"));
    scene.add(cube, Mesh{resources.add(generateCube()), generateDefaultMaterial(resources)});

    return camera;
}

} // namespace Engine
