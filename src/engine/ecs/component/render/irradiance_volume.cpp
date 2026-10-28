#include "ecs/component/render/irradiance_volume.h"

#include "core/math/grid.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

glm::uvec3 IrradianceVolume::clampedResolution(const IrradianceVolume& volume) {
    const glm::uvec3 asked(volume.resolutionX, volume.resolutionY, volume.resolutionZ);
    return Math::fitGrid(asked, 0, MAX_RESOLUTION, MAX_PROBES);
}

EntityId findIrradianceVolume(const Scene& scene) {
    return findLowestSlot<IrradianceVolume, Transform>(
        scene,
        [](const IrradianceVolume&, const Transform&) { return true; }
    );
}

} // namespace Vkm::Engine
