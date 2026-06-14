#define VKM_LOG_CATEGORY "ENGINE_APP"

#include "engine_app.h"

#include <memory>

#include "logger.h"

#include "core/engine.h"
#include "system/animation/animation_system.h"
#include "system/async/async_loader_system.h"
#include "system/event/event_system.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/physics/physics_system.h"
#include "system/script/behavior_system.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "system/camera/camera_controller.h"
#include "io/project_paths.h"

#include "gl_backend.h"

#include "default_scene.h"

namespace Engine {

EngineAppSystems setupEngineApp(Engine& engine) {
    // Gameplay behaviors must already be registered (by the caller: the runtime
    // static-links + calls registerGameBehaviors(); the editor loads the game
    // module first) - the default scene below creates them through the registry.

    auto& cameraController = engine.addSystem<CameraController>(SystemStage::Input);
    auto& eventSystem      = engine.addSystem<EventSystem>     (SystemStage::Simulation);
    // AsyncLoaderSystem drains completed asset loads before the visibility +
    // render sweep, so freshly decoded textures reach the GPU the same frame.
    engine.addSystem<AsyncLoaderSystem>(SystemStage::Simulation);
    // Gameplay scripts run before animation + physics (events -> gameplay ->
    // animation -> physics), so a behavior can set state the same frame those
    // integrate it. Takes the EventSystem to hand behaviors gameplay pub/sub.
    engine.addSystem<BehaviorSystem>  (SystemStage::Simulation, eventSystem);
    engine.addSystem<AnimationSystem> (SystemStage::Simulation);
    engine.addSystem<PhysicsSystem>   (SystemStage::Simulation, eventSystem);
    engine.addSystem<HierarchySystem> (SystemStage::Transform);
    auto& visibilitySystem = engine.addSystem<VisibilitySystem>(SystemStage::Visibility);
    auto& renderSystem     = engine.addSystem<RenderSystem>    (SystemStage::Render);

    // The minimal OpenGL backend compiles its own shader (shaders/forward) and
    // owns its single forward pass - no shader-asset registration or pass
    // pipeline wiring is needed here yet.
    renderSystem.setBackend(std::make_unique<GLBackend>());

    // Default scene: a single cube at the origin under a directional light.
    auto cameraEntity = generateDefaultScene(engine);
    cameraController.setCameraEntity(cameraEntity);

    return EngineAppSystems{ cameraController, eventSystem, visibilitySystem, renderSystem };
}

} // namespace Engine
