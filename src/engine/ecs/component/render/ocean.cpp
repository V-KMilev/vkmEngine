#include "ecs/component/render/ocean.h"

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

EntityId findOcean(const Scene& scene) {
    return findLowestSlot<Ocean, Transform>(scene, [](const Ocean&, const Transform&) { return true; });
}

} // namespace Vkm::Engine
