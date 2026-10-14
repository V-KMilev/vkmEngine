/*
 * Quasi-random sequence + GGX importance sampling.
 */

#include "constants.glsl"
#include "basis.glsl"

float radicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), radicalInverseVdC(i));
}

// Sample a GGX-distributed half-vector around N (perceptual roughness in).
vec3 importanceSampleGGX(vec2 xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    vec3 tangent, bitangent;
    basisAcross(N, tangent, bitangent);
    return normalize(tangent * h.x + bitangent * h.y + N * h.z);
}

// The level of a cube @p envRes texels a face that a sample covering @p saSample steradians reads,
// so a low pdf reads a coarser mip (Karis's prefiltered importance sampling).
float cubeMipForSolidAngle(float saSample, float envRes) {
    const float saTexel = 4.0 * PI / (6.0 * envRes * envRes);
    return max(0.5 * log2(saSample / saTexel), 0.0);
}
