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
    /// Path to the HDR equirect that's baked into the probe's cubemap
    /// set. Empty = the probe uses the global IBL bake as its source,
    /// useful when probes only need parallax-correction (not different
    /// content) - though parallax correction itself lands in a follow-up.
    std::string hdrPath;

    /// Sphere of influence in world units. Fragments outside `radius`
    /// get zero weight from this probe.
    float radius = 5.0f;

    /// Inner radius (as a fraction of `radius`) inside which the probe
    /// contributes at full strength. Between this and 1.0 the weight
    /// smoothsteps down. 0 = the probe peaks only at the centre.
    float falloffRange = 0.7f;

    /// Linear-HDR intensity multiplier on top of the bake.
    float intensity = 1.0f;

    /// Tracks whether the bake is up to date with @ref hdrPath. Bumped
    /// by the inspector when the path or other bake-affecting fields
    /// change; the backend's bake pass compares it to its own cached
    /// version and re-bakes when they differ.
    int   bakeVersion = 0;
};

} // namespace Engine
