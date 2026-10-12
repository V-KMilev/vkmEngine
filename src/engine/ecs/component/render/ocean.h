#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief A ridge of water rolling across the sea, raised by gameplay: a straight crest of finite
 *        length, rising and falling as a cosine across the way it rolls, its ends tapered away.
 *
 * Part of the water like any wave, so it is shaded, foamed and floated on as the sea is; unlike
 * the wind's waves it is not faded by the ocean's calm radius. Its crest foams.
 */
struct OceanSurge {
    glm::vec2 centre     = glm::vec2(0.0f);  ///< World X and Z: the crest's middle.
    glm::vec2 direction  = {0.0f, 1.0f};     ///< On the XZ plane: the way it rolls; zero is no surge.
    float     height     = 0.0f;             ///< Metres over the water's rest at the crest; 0 is none.
    float     halfWidth  = 1.0f;             ///< Metres from the crest to the sea, along its roll.
    float     halfLength = 4.0f;             ///< Metres from its middle to each end, along its crest.
};

/**
 * @brief The scene's sea: wind-driven waves moving every surface whose material is `waves`.
 *
 * The waves are drawn from an ocean wave spectrum - JONSWAP, shortened by TMA's correction
 * where `depth` is set (Horvath 2015) - as a sum of Gerstner waves, moved per vertex on the GPU
 * and, by the same sum, by OceanSurface for gameplay that floats things on it. A scene uses
 * one: @ref findOcean picks it. Its Transform is where the waves are whole - past `calmRadius`
 * from it they die away, gone at `stillRadius` - and the height its water rests at, which its
 * `waves` surfaces should share: what breaks the water foams by it.
 */
struct Ocean {
    static constexpr uint32_t MAX_WAVES  = 64;
    static constexpr uint32_t MAX_SURGES = 4;
    /// Metres across the square of sea ahead of the eye where the water meets what stands in it.
    static constexpr float    WATERLINE_SQUARE = 128.0f;

    float windSpeed     = 8.0f;    ///< m/s, ten metres up: the waves' energy grows nearly with its square.
    float windDirection = 0.0f;    ///< Degrees round from +Z toward +X: the way the waves roll.
    float fetch         = 100.0f;  ///< Kilometres of open water the wind has blown over.
    /// Metres of water under the waves: shallower slows them and flattens the long ones. 0 is deep.
    float depth         = 0.0f;
    float spread        = 0.5f;    ///< 0 every wave rolls with the wind; 1 they come from all ahead of it.
    float choppiness    = 0.7f;    ///< 0 round swells; 1 crests drawn to a point.
    float heightScale   = 1.0f;    ///< On the heights the spectrum gives.
    float minWavelength = 2.0f;    ///< Metres; shorter are ripples, per pixel.
    float maxWavelength = 200.0f;  ///< Metres.
    uint32_t waveCount  = 32;      ///< Waves summed, up to MAX_WAVES.
    uint32_t seed       = 1;       ///< Which waves and ripples: their directions and phases.
    /// Metres: the spectrum on below minWavelength, down to this, are ripples - every one of
    /// them, for the surface's light and foam alone, too short to move a vertex or anything afloat.
    float    shortestRipple = 0.03f;

    float calmRadius  = 0.0f;  ///< Metres from the Transform where the waves begin to fade.
    float stillRadius = 0.0f;  ///< Where they are gone; at or inside calmRadius they never fade.

    float     foam      = 0.5f;  ///< How readily a squeezed crest whitens: 0 never, 1 at the least squeeze.
    /// Metres of foam where the water meets anything standing in it, over WATERLINE_SQUARE
    /// ahead of the eye: 0 none.
    float     waterlineFoam = 0.6f;
    glm::vec3 foamColor = glm::vec3(0.9f, 0.94f, 0.96f);  ///< Linear.
    /// Metres across one repeat of the material's maps, laid over the water and drifting with the wind.
    float detailScale = 8.0f;

    /// The surges rolling now, set by gameplay as it runs: session state, not saved with the
    /// scene, so it is not reflected. An unused one has height 0.
    std::array<OceanSurge, MAX_SURGES> surges{};
};

/**
 * @brief The scene's ocean: the lowest-slot entity carrying one.
 *
 * A Transform is required too.
 *
 * @param scene Scene searched.
 * @return The ocean entity, or {} when there is none.
 */
EntityId findOcean(const Scene& scene);

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Ocean)
    VKM_F(windSpeed)
    VKM_F(windDirection)
    VKM_F(fetch)
    VKM_F(depth)
    VKM_F(spread)
    VKM_F(choppiness)
    VKM_F(heightScale)
    VKM_F(minWavelength)
    VKM_F(maxWavelength)
    VKM_F(waveCount)
    VKM_F(seed)
    VKM_F(shortestRipple)
    VKM_F(calmRadius)
    VKM_F(stillRadius)
    VKM_F(foam)
    VKM_F(waterlineFoam)
    VKM_F(foamColor)
    VKM_F(detailScale)
VKM_REFLECT_END()
