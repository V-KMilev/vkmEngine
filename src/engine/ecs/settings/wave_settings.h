#pragma once

#include <array>
#include <cstdint>
#include <tuple>

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief A ridge of water rolling across the sea, raised by gameplay: a straight crest of finite
 *        length, rising and falling as a cosine across the way it rolls, its ends tapered away.
 *
 * Part of the waves like any, so each Water shows, foams and floats things on its share of it;
 * unlike the wind's waves it is not faded by the calm radius. Its crest foams.
 */
struct WaveSurge {
    glm::vec2 center     = glm::vec2(0.0f);  ///< World X and Z: the crest's middle.
    glm::vec2 direction  = {0.0f, 1.0f};     ///< On the XZ plane: the way it rolls; zero is no surge.
    float     height     = 0.0f;             ///< Meters over the water's rest at the crest; 0 is none.
    float     halfWidth  = 1.0f;             ///< Meters from the crest to the sea, along its roll.
    float     halfLength = 4.0f;             ///< Meters from its middle to each end, along its crest.
};

/**
 * @brief What of the waves shapes them: every field their spectrum (WaveSpectrum::of) and swell
 *        (Swell::of) are made from, and no other, so waves of one shape have one swell.
 */
struct WaveShape {
    float    windSpeed     = 8.0f;    ///< m/s, ten meters up: the waves' energy grows nearly with its square.
    float    windDirection = 0.0f;    ///< Degrees round from +Z toward +X: the way the waves roll.
    float    fetch         = 100.0f;  ///< Kilometers of open water the wind has blown over.
    float    depth         = 0.0f;    ///< Meters of water under the waves: shallower slows them and flattens
                                      ///< the long ones. 0 is deep.
    float    spread        = 0.5f;    ///< 0 every wave rolls with the wind; 1 they come from all ahead of it.
    float    choppiness    = 0.7f;    ///< 0 round swells; 1 crests drawn to a point.
    float    heightScale   = 1.0f;    ///< On the heights the spectrum gives.
    float    minWavelength = 2.0f;    ///< Meters; shorter are ripples, per pixel.
    float    maxWavelength = 200.0f;  ///< Meters: the longest wave, and the largest grid's repeat - unless
                                      ///< the band fits one grid, which then repeats as far as minWavelength
                                      ///< still resolves (Swell).
    uint32_t seed          = 1;       ///< Which waves and ripples: their heights and phases.

    bool operator==(const WaveShape& other) const;
    bool operator!=(const WaveShape& other) const { return !(*this == other); }
};

/**
 * @brief The scene's waves: wind-driven, moving each Water by its share of them (Water::waves).
 *
 * The waves are drawn from an ocean wave spectrum - JONSWAP, shortened by TMA's correction where
 * `depth` is set (Horvath 2015) - every wavenumber of it a wave, on repeating grids transformed to
 * the water's movement (Swell, Tessendorf 2001), moved per vertex on the GPU and, by the same
 * grids, by WaveSurface for gameplay that floats things on it. Past `calmRadius` from `calmCenter`
 * they die away, gone at `stillRadius`.
 */
struct WaveSettings {
    static constexpr uint32_t MAX_SURGES      = 4;
    static constexpr float    SHORTEST_RIPPLE = 0.03f;  ///< Meters: the spectrum below minWavelength, down to
                                                        ///< this, is ripples - every one of them, for the
                                                        ///< surface's light and foam alone, too short to move
                                                        ///< a vertex or anything afloat.
    static constexpr float    DETAIL_SCALE    = 8.0f;   ///< Meters across one repeat of water's material
                                                        ///< maps, laid over the water and drifting with the
                                                        ///< wind.

    WaveShape shape;  ///< What the waves are made from: their spectrum, swell and ripples.

    glm::vec2 calmCenter  = glm::vec2(0.0f);  ///< World X and Z the calm is measured from.
    float     calmRadius  = 0.0f;             ///< Meters from calmCenter where the waves begin to fade.
    float     stillRadius = 0.0f;             ///< Where they are gone; at or inside calmRadius, no fade.

    std::array<WaveSurge, MAX_SURGES> surges{};  ///< The surges rolling now, set by gameplay as it runs:
                                                 ///< session state, not saved with the scene, so it is not
                                                 ///< reflected. An unused one has height 0.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::WaveShape)
    VKM_F(windSpeed, 0.0f, 40.0f, "The waves' energy grows nearly with its square")
    VKM_F(windDirection, -360.0f, 360.0f, "Round from +Z toward +X: the way the waves roll")
    VKM_F(fetch, 0.1f, 2000.0f, "Open water the wind has blown over")
    VKM_F(depth, 0.0f, 500.0f, "Shallower slows and flattens the waves; 0 is deep")
    VKM_F(spread, 0.0f, 1.0f, "0 every wave rolls with the wind, 1 from all ahead of it")
    VKM_F(choppiness, 0.0f, 1.0f, "0 round swells, 1 crests drawn to a point")
    VKM_F(heightScale, 0.0f, 10.0f, "Scales the heights the spectrum gives")
    VKM_F(minWavelength, 0.1f, 1000.0f, "Shorter are ripples: above twice the mesh's spacing")
    VKM_F(maxWavelength, 0.2f, 5000.0f, "The longest wave")
    VKM_F(seed, 0.0f, 1000000.0f, "Which waves and ripples")
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::WaveSettings)
    VKM_F(shape)
    VKM_F(calmCenter, -100000.0f, 100000.0f, "World X and Z where the waves are whole")
    VKM_F(calmRadius, 0.0f, 100000.0f, "Past it the waves begin to fade")
    VKM_F(stillRadius, 0.0f, 100000.0f, "Where they are gone; inside the calm radius never")
VKM_REFLECT_END()

namespace Vkm::Engine {

// By the reflected fields, so a field saved is a field compared: a swell is made again for any.
inline bool WaveShape::operator==(const WaveShape& other) const {
    constexpr auto fields = Reflect::Traits<WaveShape>::fields();
    return std::apply([&](const auto&... f) { return ((this->*(f.ptr) == other.*(f.ptr)) && ...); }, fields);
}

} // namespace Vkm::Engine
