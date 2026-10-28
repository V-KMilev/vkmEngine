#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/render/light.h"

namespace Vkm::Engine {

/**
 * @brief One light affecting the frame, resolved to world space.
 *
 * Covers every LightType; area lights carry rotation and size folded into two
 * world-space half-extent axes.
 */
struct LightData {
    LightType type;

    /**
     * @brief The entity slot this light was gathered from.
     *
     * A stable tie-break (as findLowestSlot) for scarce shadow atlas slots:
     * list order is SparseSet packing, which moves when an unrelated light is
     * destroyed.
     */
    uint32_t  entitySlot = 0;
    glm::vec3 color;
    float     intensity;
    float     wave = 1.0f;  ///< The share of intensity it shines with this frame (flickerWave)
    glm::vec3 position;

    glm::vec3 direction;    ///< Travel direction; used by directional/spot

    float radius;           ///< Attenuation radius (point/spot/area)

    float innerConeAngle;   ///< Spot: full-brightness cone (radians)
    float outerConeAngle;   ///< Spot: falloff edge (radians)

    // Area-light fields, zero for punctual lights.
    glm::vec3 axisU{0.0f};  ///< Rect/Disk: half-right world axis
    glm::vec3 axisV{0.0f};  ///< Rect/Disk: half-up world axis
    bool      twoSided;     ///< Rect/Disk: emit from both faces

    bool  castShadows;
    float shadowBias;       ///< See Light::shadowBias
    float shadowNormalBias; ///< See Light::shadowNormalBias
    float shadowDistance;   ///< Directional only: world distance the cascades cover.
    int   shadowPriority;   ///< See Light::shadowPriority

    /**
     * @brief Light::sourceRadius: radians for a directional light, metres otherwise.
     *
     * Sizes a directional or spot light's penumbra, and a directional light's
     * highlight. Zero is a point.
     */
    float sourceRadius = 0.0f;

    /// Whether a capture takes it: Light::inCaptures, and never a particle emitter's
    /// (ParticleEmitter::lightIntensity), which is gone in a moment and casts no shadow either.
    bool inCaptures = true;

    bool movable = true;  ///< Light::movable: off, its shadow on fixed meshes is their lightmap's.
};

/**
 * @brief The lowest-slot directional light in a frame's light list.
 *
 * findKeyLight's rule, for a backend that holds the view and not the scene.
 * Lowest slot rather than first, since list order moves with SparseSet packing.
 *
 * @param lights The frame's lights.
 * @return The key light, or null when the frame has no directional light.
 */
inline const LightData* lowestSlotDirectional(const std::vector<LightData>& lights) {
    const LightData* key = nullptr;
    for (const LightData& light : lights) {
        if (light.type != LightType::Directional) continue;
        if (!key || light.entitySlot < key->entitySlot) key = &light;
    }
    return key;
}

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
