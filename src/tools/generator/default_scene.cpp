#include "generator/default_scene.h"

#include <utility>

#include <glm/gtc/quaternion.hpp>

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

    // Negative Z, because a camera looks along its forward and this engine's
    // forward is +Z: parked at +6 it would face away from the cube it is here
    // to show. The positive pitch tilts that forward down onto the origin from
    // eye height.
    cameraTransform.position = {0.0f, 2.0f, -6.0f};
    cameraTransform.rotation = glm::quat(glm::vec3(glm::radians(18.0f), 0.0f, 0.0f));

    scene.add(camera, cameraTransform);
    scene.add(camera, Camera{});
    scene.add(camera, makeName("Camera"));

    const Entity sun = scene.createEntity();
    Transform sunTransform;
    sunTransform.position = {0.0f, 8.0f, 0.0f};

    // A directional light shines along its Transform's forward, and this
    // engine's forward is +Z (Math::computeForward), so an unrotated light
    // lies flat along the ground rather than shining down it. The pitch is
    // POSITIVE to tilt +Z downward; the yaw puts the shadow off-axis so the
    // cube reads as a solid rather than a flat front face.
    sunTransform.rotation =
        glm::quat(glm::vec3(glm::radians(50.0f), glm::radians(30.0f), 0.0f));

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
