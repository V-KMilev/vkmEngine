#include "offline/gl_capture_lighting.h"

#include <glm/glm.hpp>

#include "core/fnv1a.h"
#include "system/render/data/light_data.h"

namespace Vkm::Engine {

namespace {

// Whether @p light can light anything in @p region: a directional always, any other as far as its
// radius reaches.
bool reaches(const LightData& light, const Math::AABB& region) {
    if (light.type == LightType::Directional) return true;
    const glm::vec3 nearest = glm::clamp(light.position, region.min, region.max);
    const glm::vec3 apart   = nearest - light.position;
    return glm::dot(apart, apart) <= light.radius * light.radius;
}

} // namespace

static_assert(sizeof(LightData) == 116, "a field added to LightData goes in lightingDigest, or says why not");

uint64_t lightingDigest(const std::vector<LightData>& lights, const Math::AABB* region) {
    uint64_t digest = FNV1A_OFFSET_BASIS;
    const auto mix = [&digest](const auto& field) { digest = fnv1a64Bytes(&field, sizeof(field), digest); };
    for (const LightData& light : lights) {
        if (!light.inCaptures || (region && !reaches(light, *region))) continue;
        mix(light.type);
        mix(light.color);
        mix(light.intensity);
        mix(light.sourceRadius);
        switch (light.type) {
            case LightType::Directional:
                mix(light.entitySlot);
                mix(light.direction);
                mix(light.castShadows);
                mix(light.shadowBias);
                mix(light.shadowNormalBias);
                break;
            case LightType::Point:
                mix(light.position);
                mix(light.radius);
                break;
            case LightType::Spot:
                mix(light.position);
                mix(light.radius);
                mix(light.direction);
                mix(light.innerConeAngle);
                mix(light.outerConeAngle);
                break;
            case LightType::Rect:
            case LightType::Disk:
                mix(light.position);
                mix(light.direction);
                mix(light.radius);
                mix(light.axisU);
                mix(light.axisV);
                mix(light.twoSided);
                break;
            case LightType::Count:
                break;
        }
    }
    return digest;
}

} // namespace Vkm::Engine
