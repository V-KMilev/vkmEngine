#include "generator/default_scene.h"

#include <glm/gtc/quaternion.hpp>

#include "resource/resource_manager.h"

#include "ecs/scene.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/mesh.h"

#include "generator/light_generators.h"
#include "generator/material_generators.h"
#include "generator/mesh_generators.h"

namespace Vkm::Engine {

EntityId buildDefaultScene(Scene& scene, ResourceManager& resources) {
    const EntityId camera = scene.createEntity();
    Transform cameraTransform;

    // Negative Z, because a camera looks along its forward and this engine's
    // forward is +Z: parked at +6 it would face away from the cube it is here
    // to show. The positive pitch tilts that forward down onto the origin from
    // eye height.
    cameraTransform.position = {0.0f, 2.0f, -6.0f};
    cameraTransform.rotation = glm::quat(glm::vec3(glm::radians(18.0f), 0.0f, 0.0f));

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
    scene.add(cube, Mesh{resources.add(generateCube()), generateDefaultMaterial(resources)});

    return camera;
}

} // namespace Vkm::Engine
