#pragma once

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Makes an entity's Mesh water: drawn as water, and floated on (WaterSystem).
 *
 * A pond, a river, a pool or the sea, each its mesh's surface at its own level. Its material holds
 * its optics - albedo, roughness, ior, and the scattering it takes in by subsurface and its color;
 * this says how the scene's waves and ripples move it and what it foams. A sea is water whose waves
 * share is 1: the same water, other values. A mesh that takes waves wants its vertices closer than
 * half the waves' minWavelength, as the swell moves them. Its limits: docs/reference/water.md, "Limits".
 */
struct Water {
    float waves     = 0.0f;   ///< Share of the scene's swell and surges moving the surface and what floats on
                              ///< it: 0 a pond or a puddle, 1 the sea. A surface with waves casts no shadow;
                              ///< its waves assume it lies level.
    float ripples   = 0.5f;   ///< Share of the scene's ripples it shows: 1 open water, toward 0 a sheltered
                              ///< pool.
    float flow      = 0.0f;   ///< Meters a second it runs along the mesh's +U: a river follows its strip's
                              ///< UVs, and carries what floats on it. 0 is still.
    float depth     = 10.0f;  ///< Meters it reaches below its surface: a point deeper is under it, not in it,
                              ///< so a pool on a deck keeps what is under the deck dry. 0 has no bottom: the
                              ///< open sea.
    float foam      = 0.0f;   ///< How readily a squeezed crest whitens: 0 never, 1 at the least squeeze.
    float waterline = 0.0f;   ///< Meters of foam where it meets anything standing in it: 0 none.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Water)
    VKM_F(waves, 0.0f, 1.0f, "Share of the scene's waves moving it: 0 a pond, 1 the sea")
    VKM_F(ripples, 0.0f, 1.0f, "Share of the scene's ripples it shows: 1 open water")
    VKM_F(flow, -20.0f, 20.0f, "How fast it runs along the mesh's +U: a river's strip")
    VKM_F(depth, 0.0f, 1000.0f, "How far below its surface it reaches; 0 has no bottom")
    VKM_F(foam, 0.0f, 1.0f, "How readily its squeezed crests whiten")
    VKM_F(waterline, 0.0f, 10.0f, "Foam where it meets what stands in it; 0 none")
VKM_REFLECT_END()
