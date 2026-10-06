#include "ecs/component/render/camera.h"

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

EntityId findActiveCamera(const Scene& scene, EntityId cached) {
    const Camera* held = scene.tryGet<Camera>(cached);
    if (held && held->active && scene.has<Transform>(cached)) {
        return cached;
    }

    return findLowestSlot<Camera, Transform>(
        scene,
        [](const Camera& camera, const Transform&) { return camera.active; }
    );
}

} // namespace Vkm::Engine
