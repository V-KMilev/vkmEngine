#include "light_generators.h"

namespace Engine {

Light generateDirectionalLight(
    const glm::vec3& color,
    float intensity,
    bool castShadows
) {
    Light light;
    light.type = LightType::Directional;
    light.color = color;
    light.intensity = intensity;
    light.castShadows = castShadows;
    light.enabled = true;

    // Directional lights don't use radius or cone angles
    light.radius = 0.0f;
    light.innerConeAngle = 0.0f;
    light.outerConeAngle = 0.0f;

    return light;
}

Light generatePointLight(
    const glm::vec3& color,
    float intensity,
    float radius,
    bool castShadows
) {
    Light light;
    light.type = LightType::Point;
    light.color = color;
    light.intensity = intensity;
    light.radius = radius;
    light.castShadows = castShadows;
    light.enabled = true;

    // Point lights don't use cone angles
    light.innerConeAngle = 0.0f;
    light.outerConeAngle = 0.0f;

    return light;
}

Light generateSpotLight(
    const glm::vec3& color,
    float intensity,
    float radius,
    float innerConeAngle,
    float outerConeAngle,
    bool castShadows
) {
    Light light;
    light.type = LightType::Spot;
    light.color = color;
    light.intensity = intensity;
    light.radius = radius;
    light.innerConeAngle = innerConeAngle;
    light.outerConeAngle = outerConeAngle;
    light.castShadows = castShadows;
    light.enabled = true;

    return light;
}

Light generateRectLight(
    const glm::vec3& color,
    float intensity,
    float width,
    float height,
    float radius,
    bool twoSided,
    bool castShadows
) {
    Light light;
    light.type        = LightType::Rect;
    light.color       = color;
    light.intensity   = intensity;
    light.radius      = radius;
    light.areaWidth   = width;
    light.areaHeight  = height;
    light.twoSided    = twoSided;
    light.castShadows = castShadows;
    light.enabled     = true;

    // Cone angles are unused for area lights; clear so the inspector
    // doesn't show stale spot defaults.
    light.innerConeAngle = 0.0f;
    light.outerConeAngle = 0.0f;

    return light;
}

Light generateDiskLight(
    const glm::vec3& color,
    float intensity,
    float areaRadius,
    float radius,
    bool twoSided,
    bool castShadows
) {
    Light light;
    light.type        = LightType::Disk;
    light.color       = color;
    light.intensity   = intensity;
    light.radius      = radius;
    light.areaRadius  = areaRadius;
    light.twoSided    = twoSided;
    light.castShadows = castShadows;
    light.enabled     = true;

    light.innerConeAngle = 0.0f;
    light.outerConeAngle = 0.0f;

    return light;
}

} // namespace Engine
