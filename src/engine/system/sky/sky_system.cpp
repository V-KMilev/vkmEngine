#include "system/sky/sky_system.h"

#include <algorithm>

#include <glm/gtc/quaternion.hpp>

#include "core/math/rotation.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/light.h"
#include "ecs/hierarchy_operations.h"
#include "system/sky/atmosphere.h"

namespace Vkm::Engine {

void SkySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("SkySystem");

    const Environment& env = ctx.scene.environment();
    if (!env.sky.procedural) return;

    const EntityId key = findKeyLight(ctx.scene);
    if (!key) return;

    // A swap, not a blend: interpolating opposite bodies would sweep the light through
    // directions neither occupies. Both fades reach zero at the horizon, where it swaps.
    constexpr float FADE = NightSkySettings::TWILIGHT_DEGREES;
    const float sunUp    = std::clamp(env.sky.sunElevation / FADE, 0.0f, 1.0f);
    const float moonUp   = std::clamp(-env.sky.sunElevation / FADE, 0.0f, 1.0f);
    const bool  moonOwns = sunUp <= 0.0f;

    const SkyAngles angles = moonOwns ? env.moonAngles() : env.sunAngles();

    // Opposite Environment::directionFromAngles, which points toward the body:
    // a light travels away from its source.
    const glm::vec3 euler(glm::radians(-angles.elevation), glm::radians(angles.azimuth), 0.0f);
    const glm::quat rotation = glm::quat(euler);

    const glm::vec3 color     = moonOwns ? env.night.moonlightColor : Atmosphere::sunlight(env.sky);
    const float     intensity = moonOwns
        ? env.night.moonlightIntensity * moonUp
        : env.sky.lightIntensity * sunUp;

    // The rotation is world-space and a Transform local, so the parent's turn is
    // divided out; walked because WorldTransform is a frame behind here.
    const Hierarchy* link        = ctx.scene.tryGet<Hierarchy>(key);
    const EntityId   parent      = link ? link->parent : EntityId{};
    const glm::quat  parentWorld = parent
        ? Math::worldRotationOf(HierarchyOperations::computeWorldMatrix(ctx.scene, parent))
        : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    Transform& transform = ctx.scene.get<Transform>(key);
    Light&     light     = ctx.scene.get<Light>(key);
    transform.rotation = glm::inverse(parentWorld) * rotation;
    light.color        = color;
    light.intensity    = intensity;
}

} // namespace Vkm::Engine
