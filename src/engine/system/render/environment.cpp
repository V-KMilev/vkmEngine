#include "system/render/environment.h"

#include "ecs/scene.h"
#include "ecs/component/name.h"

namespace Engine {

EnvironmentConfig* tryGetSceneEnvironment(Scene& scene) {
    EnvironmentConfig* found = nullptr;
    scene.forEach<EnvironmentConfig>([&](EntityId, EnvironmentConfig& e) {
        if (!found) found = &e;
    });
    return found;
}

EnvironmentConfig& sceneEnvironment(Scene& scene) {
    if (EnvironmentConfig* existing = tryGetSceneEnvironment(scene)) {
        return *existing;
    }
    Entity entity = scene.createEntity();
    scene.add(entity, Name{"Environment"});
    return scene.add(entity, EnvironmentConfig{});
}

} // namespace Engine
