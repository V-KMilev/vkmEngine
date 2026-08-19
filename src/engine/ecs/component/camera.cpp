#include "ecs/component/camera.h"

#include "ecs/scene.h"
#include "ecs/component/transform.h"

namespace Vkm::Engine {

EntityId findActiveCamera(const Scene& scene, EntityId cached) {
    if (cached && scene.isAlive(cached)
        && scene.has<Camera>(cached) && scene.has<Transform>(cached)
        && scene.get<Camera>(cached).active) {
        return cached;
    }

    EntityId active{};
    scene.forEach<Camera, Transform>([&](EntityId id, const Camera& camera, const Transform&) {
        if (active || !camera.active) return;
        active = id;
    });
    return active;
}

} // namespace Vkm::Engine
