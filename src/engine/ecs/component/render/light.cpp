#include "ecs/component/render/light.h"

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

EntityId findKeyLight(const Scene& scene) {
    return findLowestSlot<Light, Transform>(scene,
        [](const Light& light, const Transform&) {
            return light.type == LightType::Directional;
        });
}

} // namespace Vkm::Engine
