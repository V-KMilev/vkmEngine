/*
 * Shared BRDF pieces. Requires constants.glsl (PI).
 *
 * Contract: distributionGGX takes the GGX ALPHA (= perceptual roughness
 * squared), Filament-style. Callers holding perceptual roughness square it
 * first - the same name previously existed with both signatures in different
 * files, which silently mis-squared when a call was moved between them.
 */

// GGX / Trowbridge-Reitz normal distribution (Karis stable form).
float distributionGGX(float NdotH, float a) {
    float a2 = a * a;
    float d  = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}
