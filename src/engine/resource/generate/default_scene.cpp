#include "resource/generate/default_scene.h"

#include <glm/gtc/quaternion.hpp>

#include "resource/resource_manager.h"

#include "ecs/scene.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/mesh.h"

#include "resource/generate/light_generators.h"
#include "resource/generate/material_generators.h"
#include "resource/generate/mesh_generators.h"

namespace Vkm::Engine {

EntityId buildDefaultScene(Scene& scene, ResourceManager& resources) {
    const EntityId camera = scene.createEntity();
    Transform cameraTransform;

    // Back along +Z, which forward being -Z makes the natural place to watch the
    // origin from. Asked of lookRotation rather than named as angles, which would
    // only hold for one convention.
    cameraTransform.position = {0.0f, 2.0f, 6.0f};
    cameraTransform.rotation = Math::lookRotation(-cameraTransform.position);

    scene.add(camera, cameraTransform);
    scene.add(camera, Camera{});
    scene.add(camera, makeName("Camera"));

    const EntityId sun = scene.createEntity();
    Transform sunTransform;
    sunTransform.position = {0.0f, 8.0f, 0.0f};

    scene.add(sun, sunTransform);
    scene.add(sun, generateLight(LightType::Directional));
    scene.add(sun, makeName("Sun"));

    const EntityId cube = scene.createEntity();
    scene.add(cube, Transform{});
    scene.add(cube, makeName("Cube"));
    scene.add(cube, Mesh{addGeneratedMesh(resources, generateCube()), generateDefaultMaterial(resources)});

    return camera;
}

} // namespace Vkm::Engine
