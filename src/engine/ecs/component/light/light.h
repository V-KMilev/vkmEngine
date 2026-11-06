#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Enumeration of light types.
 */
enum class LightType {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
    Count               ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief Component representing a light source in the scene.
 *
 * Pose comes from the Transform; a spot shines along its forward.
 */
struct Light {
    static constexpr float SHADOW_BIAS = 0.5f;  ///< How far the depth compare slides toward the light, in
                                                ///< texels of the light's own map at the receiver, so one
                                                ///< value holds for every light type, range and distance;
                                                ///< grown where the light grazes (biasSlide in
                                                ///< shaders/opengl/include/shadows.glsl).

    static constexpr float SHADOW_NORMAL_BIAS = 1.0f;  ///< How far the point moves off the surface before the
                                                       ///< compare, in shadow texels: along the surface's
                                                       ///< normal, by this times the sine of the light's
                                                       ///< angle to it, nothing head-on and all of it where
                                                       ///< the light grazes.

    LightType type      = LightType::Directional;
    glm::vec3 color     = {1.0f, 1.0f, 1.0f};
    float     intensity = 1.0f;

    float radius = 10.0f;  ///< Attenuation cutoff (point/spot)

    float innerConeAngle = 0.5f;    ///< Spot: full-brightness cone, radians
    float outerConeAngle = 0.785f;  ///< Spot: falloff edge, radians

    bool castShadows = true;

    float shadowDistance = 100.0f;  ///< Directional only: world distance the cascades cover.

    int shadowPriority = 0;  ///< Which lights keep a shadow when more cast than the atlas holds; higher
                             ///< first. Among equals the nearest to the camera wins. Unread on a directional
                             ///< light: one always comes before any spot or point, and the lowest slot casts.

    float sourceRadius = glm::radians(0.5f);  ///< How large the light's source is: directional, an angular
                                              ///< radius in radians (the sun is about 0.0047); spot and
                                              ///< point, an emitter radius in meters (a sphere light). It
                                              ///< sizes the highlight, a baked shadow's penumbra and the
                                              ///< sky's sun; lighting.md says what it does to a live shadow.

    float flicker = 0.0f;  ///< How deep the light wavers, as a fire's does: 0 steady, 1 dips to dark. Shading
                           ///< alone, by the world's clock (flickerWave): the intensity stays what the scene
                           ///< saves and gameplay reads, and a probe or the irradiance volume bakes it
                           ///< steady. A directional light does not flicker.

    bool inCaptures = true;  ///< Whether a reflection probe or the irradiance volume takes it. A capture made
                             ///< under other light is made again, so one near a light that never holds still
                             ///< - carried, swinging, floating - is never done: leave such a light out.

    bool movable = true;  ///< Off: it never moves or changes while the world runs, and the shadows fixed
                          ///< meshes (Mesh::movable off) cast in its light are baked into the lightmaps they
                          ///< fall on, soft whatever the penumbra, and read there in place of its shadow map;
                          ///< what moves is still compared live. The bake takes the first few such lights a
                          ///< scene holds.

    bool enabled = true;
};

/**
 * @brief The share of its intensity @p light shines with at @p seconds: 1 when steady or
 *        directional, down to 1 - flicker as it wavers.
 *
 * Three sines at rates no two of which share a short beat - together they repeat only every
 * 100/6 seconds - and each light runs ahead by its entity's @p slot, so two fires do not flicker as
 * one.
 *
 * @param light   Its flicker.
 * @param seconds The world's clock, as NetSession::worldTime gives it.
 * @param slot    The light's entity slot.
 * @return The share, in [1 - flicker, 1].
 */
float flickerWave(const Light& light, double seconds, uint32_t slot);

/**
 * @brief The scene's key light: the lowest-slot entity carrying an enabled directional Light.
 *
 * A Transform is required too; ties break by lowest slot (see findLowestSlot).
 * A frame's RenderView names it among its lights (RenderView::SceneLists::keyLight).
 *
 * @param scene Scene searched.
 * @return The key light entity, or {} when there is none.
 */
EntityId findKeyLight(const Scene& scene);

/**
 * @brief Generate a light component of the given type.
 *
 * Only the fields whose sensible value depends on the type differ from the
 * Light struct's own defaults: how far the light reaches, and the cone angles,
 * which are cleared on everything but a spot.
 *
 * @param type Which kind of light to build.
 * @return A Light component configured for @p type.
 */
Light generateLight(LightType type);

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::LightType, "Directional", "Point", "Spot")

VKM_REFLECT_BEGIN(::Vkm::Engine::Light)
    VKM_F(type, "A sun, a point lamp or a spot")
    VKM_F(color)
    VKM_F(intensity, 0.0f, 100000.0f, "How bright: a sun takes hundreds, a room's lamp tens")
    VKM_F(radius, 0.1f, 1000.0f, "How far a lamp or a spot reaches")
    VKM_F(innerConeAngle, 0.0f, 1.5708f, "A spot's cone at full brightness")
    VKM_F(outerConeAngle, 0.0f, 1.5708f, "A spot's cone where its light ends")
    VKM_F(castShadows)
    VKM_F(shadowDistance, 1.0f, 1000.0f, "How far from the camera a sun's shadows reach")
    VKM_F(shadowPriority, -100.0f, 100.0f, "When the atlas is full, the highest keep their shadows")
    VKM_F(sourceRadius, 0.0f, 2.0f, "Its source's size: the larger, the softer; the sun is 0.27 deg")
    VKM_F(flicker, 0.0f, 1.0f, "How deep it wavers, as a fire does: 0 steady, 1 to dark")
    VKM_F(inCaptures, "Off for one that never holds still, or nearby probes never settle")
    VKM_F(movable, "Off for one that never moves: fixed meshes' shadows in it are baked")
    VKM_F(enabled)
VKM_REFLECT_END()
