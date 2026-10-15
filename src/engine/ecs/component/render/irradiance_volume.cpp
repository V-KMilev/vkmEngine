#include "ecs/component/render/irradiance_volume.h"

#include <algorithm>
#include <cmath>

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

glm::uvec3 IrradianceVolume::clampedResolution(const IrradianceVolume& volume) {
    const glm::uvec3 asked(volume.resolutionX, volume.resolutionY, volume.resolutionZ);
    glm::uvec3 dims = glm::min(asked, glm::uvec3(MAX_RESOLUTION));
    const double count = static_cast<double>(dims.x) * dims.y * dims.z;
    if (count <= MAX_PROBES) return dims;

    // No axis grows, so flooring keeps the product under the cap.
    const double scale = std::cbrt(MAX_PROBES / count);
    for (int axis = 0; axis < 3; ++axis) {
        const auto scaled = static_cast<uint32_t>(std::floor(dims[axis] * scale));
        dims[axis] = std::max(scaled, 1u);
    }
    return dims;
}

EntityId findIrradianceVolume(const Scene& scene) {
    return findLowestSlot<IrradianceVolume, Transform>(
        scene,
        [](const IrradianceVolume&, const Transform&) { return true; }
    );
}

} // namespace Vkm::Engine
