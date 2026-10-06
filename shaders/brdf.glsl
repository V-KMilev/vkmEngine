/*
 * The GGX lobe and its visibility term, shared by shading and the integral bake.
 *
 * distributionGGX takes the GGX ALPHA (perceptual roughness squared); passing
 * perceptual roughness is a silent mis-square, not an error.
 */

#include "constants.glsl"

// The smoothest perceptual roughness anything is shaded with; keeps
// distributionGGX's denominator off zero (d never falls below alpha^2).
const float MIN_ROUGHNESS = 0.045;

// GGX / Trowbridge-Reitz normal distribution (Karis stable form). Not floored:
// a floor large enough to matter flattens polished highlights. The peak at
// MIN_ROUGHNESS, about 77,600, is past a half float, so a shader writing it to
// an HDR target clamps to HALF_MAX.
float distributionGGX(float NdotH, float a) {
    float a2 = a * a;
    float d  = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PI * d * d);
}

// Height-correlated Smith visibility, V = G / (4 NoL NoV) (Heitz 2014), taking
// the GGX alpha.
float visSmithCorrelated(float NdotV, float NdotL, float a) {
    float a2 = a * a;
    float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}
