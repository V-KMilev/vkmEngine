#include "ecs/component/render/light.h"

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

EntityId findKeyLight(const Scene& scene) {
    const auto isKey = [](const Light& light, const Transform&) {
        return light.enabled && light.type == LightType::Directional;
    };
    return findLowestSlot<Light, Transform>(scene, isKey);
}

} // namespace Vkm::Engine
