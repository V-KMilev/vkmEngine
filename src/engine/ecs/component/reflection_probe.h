#pragma once

#include <string>

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Local reflection probe (IBL contribution authored per-location).
 *
 * The engine's baseline IBL is a single global cubemap baked from an
 * HDR equirect at startup; that one's still used as the fallback when
 * no probes cover a fragment or as the global ambient when none are
 * present. Reflection probes layer over that: each probe bakes its own
 * irradiance + prefilter cubemap from its HDR source and contributes
 * to a fragment with a distance-falloff weight.
 *
 * Pure data; rendering is driven by the forward pass + GLIBL probe
 * set. The probe's WORLD POSITION comes from the entity's Transform
 * (so moving the entity moves the probe), not from a field here.
 * Falloff is a linear smoothstep from @ref radius down to
 * radius * @ref falloffRange.
 */
struct ReflectionProbe {
    std::string hdrPath;          ///< Equirect HDR baked into this probe; empty = use the global IBL bake.
    float radius       = 5.0f;    ///< Sphere of influence; fragments outside get zero weight.
    float falloffRange = 0.7f;    ///< Inner fraction of radius at full strength; smoothsteps to 0 at radius.
    float intensity    = 1.0f;    ///< Linear-HDR multiplier applied on top of the bake.
    int   bakeVersion  = 0;       ///< Bumped by the editor when bake-affecting fields change; backend re-bakes on mismatch.
};

} // namespace Engine
