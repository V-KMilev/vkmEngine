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

// The Charlie sheen distribution (Estevez and Kulla 2017, KHR_materials_sheen), taking the sheen
// lobe's alpha (its roughness squared), held off the needle a near-zero roughness would make.
float distributionCharlie(float NdotH, float alpha) {
    float invR = 1.0 / max(alpha, 0.07);
    float cos2 = NdotH * NdotH;
    float sin2 = max(1.0 - cos2, 0.0);
    return (2.0 + invR) * pow(sin2, invR * 0.5) / (2.0 * PI);
}

// The Charlie lobe's shadowing exponent, Estevez and Kulla's fit, as the glTF sample viewer has it.
float sheenLambdaFit(float x, float alpha) {
    float t = (1.0 - alpha) * (1.0 - alpha);
    float a = mix(21.5473, 25.3245, t);
    float b = mix(3.82987, 3.32435, t);
    float c = mix(0.19823, 0.16801, t);
    float d = mix(-1.97760, -1.27393, t);
    float e = mix(-4.32054, -4.85967, t);
    return a / (1.0 + b * pow(x, c)) + d * x + e;
}

float sheenLambda(float cosTheta, float alpha) {
    if (abs(cosTheta) < 0.5) return exp(sheenLambdaFit(cosTheta, alpha));
    return exp(2.0 * sheenLambdaFit(0.5, alpha) - sheenLambdaFit(1.0 - cosTheta, alpha));
}

// The Charlie lobe's visibility, V = G / (4 NoL NoV), taking its alpha.
float visCharlie(float NdotV, float NdotL, float alpha) {
    float a = max(alpha, 1e-6);
    float g = 1.0 + sheenLambda(NdotV, a) + sheenLambda(NdotL, a);
    return clamp(1.0 / (g * 4.0 * max(NdotV * NdotL, 1e-6)), 0.0, 1.0);
}
