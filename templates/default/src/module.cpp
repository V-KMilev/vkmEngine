// The entry points a host looks for; each is documented in system/script/module_entry.h.
// Without vkmSetupNetwork, `vkm serve` refuses to start; see docs/reference/networking.md.
#include "ecs/component/render/camera.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "resource/generate/material_generators.h"
#include "resource/generate/mesh_generators.h"

#include "system/script/module_entry.h"

#include "game.h"

VKM_MODULE_ENTRY
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehaviors<Game::Spinner>();
}

VKM_MODULE_ENTRY
void vkmBuildScene(Vkm::Engine::Scene& scene, Vkm::Engine::ResourceManager& resources) {
    // Entries live at global scope, outside game.h's using-directive.
    using namespace Vkm::Engine;

    // Forward is -Z, so an unrotated camera at +Z faces the origin; Math::computeForward
    // asks an orientation which way it points.
    const EntityId camera = scene.createEntity();
    scene.add(camera, makeName("Camera"));
    scene.add(camera, Transform{{0.0f, 2.0f, 7.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});
    scene.add(camera, Camera{});

    // A directional light shines along its forward, so a sun needs a negative pitch.
    const EntityId sun = scene.createEntity();
    scene.add(sun, makeName("Sun"));
    Transform sunTransform{};
    sunTransform.rotation = glm::quat(glm::vec3(glm::radians(-50.0f), glm::radians(30.0f), 0.0f));
    scene.add(sun, sunTransform);
    Light sunLight{};
    sunLight.type = LightType::Directional;
    scene.add(sun, sunLight);

    // Generated assets are named, so a scene saved from the editor finds them again.
    const MeshHandle     cube     = resources.add(generateCube(), "game:cube");
    const MeshHandle     ball     = resources.add(generateSphere(), "game:ball");
    const MaterialHandle material = generateDefaultMaterial(resources);

    // A body needs a Collider and a Rigidbody; a Collider alone touches nothing. The
    // spinner is kinematic, its pose the Spinner's; the ball drops onto it.
    const EntityId floor = scene.createEntity();
    scene.add(floor, makeName("Floor"));
    scene.add(floor, Transform{{0.0f, -1.6f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {12.0f, 0.2f, 12.0f}});
    scene.add(floor, Mesh{cube, material});
    Collider floorShape;
    floorShape.parts[0].halfExtents = {6.0f, 0.1f, 6.0f};
    scene.add(floor, std::move(floorShape));
    Rigidbody floorBody;
    floorBody.motion = RigidbodyMotion::Static;
    scene.add(floor, std::move(floorBody));

    const EntityId spinner = scene.createEntity();
    scene.add(spinner, makeName("Spinner"));
    scene.add(spinner, Transform{});
    scene.add(spinner, Mesh{cube, material});
    scene.add(spinner, Collider{});
    Rigidbody spinnerBody;
    spinnerBody.motion = RigidbodyMotion::Kinematic;
    scene.add(spinner, std::move(spinnerBody));
    addBehavior<Game::Spinner>(scene, spinner);

    const EntityId dropped = scene.createEntity();
    scene.add(dropped, makeName("Ball"));
    scene.add(dropped, Transform{{0.2f, 3.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.6f, 0.6f, 0.6f}});
    scene.add(dropped, Mesh{ball, material});
    Collider ballShape;
    ballShape.parts[0].shape      = ColliderShape::Capsule;
    ballShape.parts[0].radius     = 0.3f;
    ballShape.parts[0].halfHeight = 0.0f;
    scene.add(dropped, std::move(ballShape));
    scene.add(dropped, Rigidbody{});
}
