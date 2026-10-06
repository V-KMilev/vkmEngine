#include "ecs/component/render/irradiance_volume.h"

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

EntityId findIrradianceVolume(const Scene& scene) {
    return findLowestSlot<IrradianceVolume, Transform>(
        scene,
        [](const IrradianceVolume&, const Transform&) { return true; }
    );
}

} // namespace Vkm::Engine
