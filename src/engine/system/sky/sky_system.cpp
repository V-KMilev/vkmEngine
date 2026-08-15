#define VKM_LOG_CATEGORY "SKY"

#include "system/sky/sky_system.h"

#include <algorithm>

#include <glm/gtc/quaternion.hpp>

#include "ecs/scene.h"
#include "ecs/component/light.h"
#include "ecs/component/transform.h"

namespace Engine {

namespace {

// Degrees above the horizon over which the sun's light fades out. A sun on the
// horizon lights almost nothing - the atmosphere has taken it - and a directional
// light does not model that on its own. Fading it here is also what makes the
// handover to moonlight invisible: the light swings round to the moon while it
// is contributing nothing either way.
constexpr float SUN_FADE_DEGREES = 8.0f;

} // namespace

void SkySystem::update(FrameContext& ctx) {
    const Environment& env = ctx.scene.environment();
    if (!env.sky.procedural) return;

    // Whichever body is up owns the light. They sit opposite each other, so this
    // is a swap and not a blend: interpolating between two near-opposite
    // directions would sweep the light through directions neither body occupies.
    const float sunUp    = std::clamp(env.sky.sunElevation / SUN_FADE_DEGREES, 0.0f, 1.0f);
    const bool  moonOwns = sunUp <= 0.0f;

    const float elevation = moonOwns ? -env.sky.sunElevation + env.night.moonTilt
                                     :  env.sky.sunElevation;
    const float azimuth   = moonOwns ?  env.sky.sunAzimuth + 180.0f
                                     :  env.sky.sunAzimuth;

    // Built from the angles rather than from the direction vector: a look-at
    // would have to be corrected for this engine's +Z forward, and the euler
    // form is exact. Elevation maps straight onto pitch; the azimuth turns by a
    // half so the light faces AWAY from the body - the direction TO it is where
    // the light comes from, not where it points.
    const glm::quat rotation = glm::quat(glm::vec3(
        glm::radians(elevation), glm::radians(azimuth + 180.0f), 0.0f));

    const glm::vec3 color     = moonOwns ? env.night.moonlightColor : env.sky.lightColor;
    const float     intensity = moonOwns ? env.night.moonlightIntensity
                                         : env.sky.lightIntensity * sunUp;

    bool pointed = false;
    ctx.scene.forEach<Light, Transform>([&](EntityId, Light& light, Transform& transform) {
        if (pointed || light.type != LightType::Directional) return;
        transform.rotation = rotation;
        light.color        = color;
        light.intensity    = intensity;
        pointed = true;
    });
}

} // namespace Engine
