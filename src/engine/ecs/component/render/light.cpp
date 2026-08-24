#include "ecs/component/render/light.h"

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

EntityId findKeyLight(const Scene& scene) {
    EntityId key{};
    scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform&) {
        if (key || light.type != LightType::Directional) return;
        key = id;
    });
    return key;
}

} // namespace Vkm::Engine
