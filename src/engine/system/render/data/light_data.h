#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/light/light.h"

namespace Vkm::Engine {

/**
 * @brief One light affecting the frame, resolved to world space.
 *
 * Covers every LightType.
 */
struct LightData {
    LightType type;

    uint32_t  entitySlot = 0;     ///< The entity slot this light was gathered from. A stable tie-break (as
                                  ///< findLowestSlot) for scarce shadow atlas slots: list order is SparseSet
                                  ///< packing, which moves when an unrelated light is destroyed.
    glm::vec3 color;
    float     intensity;
    float     wave       = 1.0f;  ///< The share of intensity it shines with this frame (flickerWave)
    glm::vec3 position;

    glm::vec3 direction;   ///< Travel direction; used by directional/spot

    float radius;          ///< Attenuation radius (point/spot)

    float innerConeAngle;  ///< Spot: full-brightness cone (radians)
    float outerConeAngle;  ///< Spot: falloff edge (radians)

    bool  castShadows;
    float shadowDistance;  ///< Directional only: world distance the cascades cover.
    int   shadowPriority;  ///< See Light::shadowPriority

    float sourceRadius = 0.0f;  ///< Light::sourceRadius; zero is a point.

    bool inCaptures = true;  ///< Whether a capture takes it: Light::inCaptures, and never a particle
                             ///< emitter's (ParticleEmitter::lightIntensity), which is gone in a moment and
                             ///< casts no shadow either.

    bool movable = true;  ///< Light::movable: off, its shadow on fixed meshes is their lightmap's.
};

/**
 * @brief The lowest-slot directional light that casts shadows: the sun a shadow is drawn for.
 *
 * Not always the key light: a key light that casts none leaves the sun's shadow to the next.
 *
 * @param lights The frame's lights.
 * @return That light, or null when no directional light casts.
 */
inline const LightData* lowestSlotDirectionalCaster(const std::vector<LightData>& lights) {
    const LightData* sun = nullptr;
    for (const LightData& light : lights) {
        if (light.type != LightType::Directional || !light.castShadows) continue;
        if (!sun || light.entitySlot < sun->entitySlot) sun = &light;
    }
    return sun;
}

} // namespace Vkm::Engine
