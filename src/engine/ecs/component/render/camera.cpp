#include "ecs/component/render/camera.h"

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

EntityId findActiveCamera(const Scene& scene, EntityId cached) {
    if (cached && scene.isAlive(cached)
        && scene.has<Camera>(cached) && scene.has<Transform>(cached)
        && scene.get<Camera>(cached).active) {
        return cached;
    }

    return findLowestSlot<Camera, Transform>(scene,
        [](const Camera& camera, const Transform&) { return camera.active; });
}

} // namespace Vkm::Engine
