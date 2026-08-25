#define VKM_LOG_CATEGORY "SKY"

#include "system/sky/sky_system.h"

#include <algorithm>

#include <glm/gtc/quaternion.hpp>

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/light.h"

namespace Vkm::Engine {

namespace {

// Degrees either side of the horizon over which the key light fades. A sun on
// the horizon lights almost nothing - the atmosphere has taken it - and a
// directional light does not model that on its own.
constexpr float SUN_FADE_DEGREES = 8.0f;

} // namespace

void SkySystem::update(FrameContext& ctx) {
    const Environment& env = ctx.scene.environment();
    if (!env.sky.procedural) return;

    // A swap, not a blend: the two sit opposite, so interpolating would sweep
    // the light through directions neither occupies. Both fades read the sun's
    // elevation - moonTilt already has the moon well up before it takes over.
    const float sunUp    = std::clamp(env.sky.sunElevation / SUN_FADE_DEGREES, 0.0f, 1.0f);
    const float moonUp   = std::clamp(-env.sky.sunElevation / SUN_FADE_DEGREES, 0.0f, 1.0f);
    const bool  moonOwns = sunUp <= 0.0f;

    // Taken from the Environment rather than re-derived here, so the light and
    // the disc the skybox draws for the same body cannot disagree.
    const SkyAngles angles = moonOwns ? env.moonAngles() : env.sunAngles();

    // From the angles, not a direction vector: a look-at would need correcting
    // for this engine's +Z forward, and the euler form is exact. The azimuth
    // turns by a half so the light faces away from the body it comes from.
    const glm::quat rotation = glm::quat(glm::vec3(
        glm::radians(angles.elevation), glm::radians(angles.azimuth + 180.0f), 0.0f));

    const glm::vec3 color     = moonOwns ? env.night.moonlightColor : env.sky.lightColor;
    const float     intensity = moonOwns ? env.night.moonlightIntensity * moonUp
                                         : env.sky.lightIntensity * sunUp;

    // Whose light this is, asked rather than decided here: the editor greys the
    // fields written just below, and it can only do that against the same rule.
    const EntityId key = findKeyLight(ctx.scene);
    if (!key) return;

    Transform& transform = ctx.scene.get<Transform>(key);
    Light&     light     = ctx.scene.get<Light>(key);
    transform.rotation = rotation;
    light.color        = color;
    light.intensity    = intensity;
}

} // namespace Vkm::Engine
