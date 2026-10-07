#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Enumeration of light types.
 *
 * Rect and Disk are area lights and cast no shadow (shading in
 * shaders/forward/pbr/fragment.shader).
 */
enum class LightType {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
    Rect        = 3,    ///< Width x height, faces -direction
    Disk        = 4,    ///< areaRadius, faces -direction
    Count               ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief Component representing a light source in the scene.
 *
 * Pose comes from the Transform. Area lights emit along -direction (a spot along
 * +direction); their intensity is point-equivalent, so from afar they light
 * like a point light of the same intensity.
 */
struct Light {
    LightType type      = LightType::Directional;
    glm::vec3 color     = {1.0f, 1.0f, 1.0f};
    float     intensity = 1.0f;

    float radius = 10.0f;                      ///< Attenuation cutoff (point/spot/area)

    float innerConeAngle = 0.5f;               ///< Spot: full-brightness cone, radians
    float outerConeAngle = 0.785f;             ///< Spot: falloff edge, radians

    float areaWidth  = 1.0f;                   ///< Rect width along the local X axis
    float areaHeight = 1.0f;                   ///< Rect height along the local Y axis
    float areaRadius = 0.5f;                   ///< Disk radius
    bool  twoSided   = false;                  ///< Area lights: emit from both faces

    bool castShadows = true;

    /**
     * @brief How far the depth compare slides toward the light, in shadow texels (0..4).
     *
     * In texels of the light's own map at the receiver, so one value holds for every
     * light type, range and distance; grown where the light grazes (biasSlide in
     * shaders/shadows.glsl).
     */
    float shadowBias = 0.5f;

    /**
     * @brief How far the point moves off the surface before the compare, in shadow texels (0..4).
     *
     * Along the surface's normal, by this times the sine of the light's angle to it:
     * nothing head-on, all of it where the light grazes.
     */
    float shadowNormalBias = 1.0f;
    float shadowDistance   = 100.0f;  ///< Directional only: world distance the cascades cover.

    /**
     * @brief How large the light's source is, which is how soft its shadows are.
     *
     * Directional: angular radius in radians (the sun is about 0.0047), also
     * sizing its highlight. Spot and point: emitter radius in metres, sizing
     * their highlight too (a sphere light). 0 is hard. The procedural sky draws
     * the key light's sun at this size.
     */
    float sourceRadius = glm::radians(0.5f);

    bool enabled = true;
};

/**
 * @brief The scene's key light: the lowest-slot entity carrying an enabled directional Light.
 *
 * A Transform is required too; ties break by lowest slot (see findLowestSlot).
 * lowestSlotDirectional applies the same rule to a frame's lights.
 *
 * @param scene Scene searched.
 * @return The key light entity, or {} when there is none.
 */
EntityId findKeyLight(const Scene& scene);

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::LightType, "Directional", "Point", "Spot", "Rect", "Disk")

VKM_REFLECT_BEGIN(::Vkm::Engine::Light)
    VKM_F(type)
    VKM_F(color)
    VKM_F(intensity)
    VKM_F(radius)
    VKM_F(innerConeAngle)
    VKM_F(outerConeAngle)
    VKM_F(areaWidth)
    VKM_F(areaHeight)
    VKM_F(areaRadius)
    VKM_F(twoSided)
    VKM_F(castShadows)
    VKM_F(shadowBias)
    VKM_F(shadowNormalBias)
    VKM_F(shadowDistance)
    VKM_F(sourceRadius)
    VKM_F(enabled)
VKM_REFLECT_END()
