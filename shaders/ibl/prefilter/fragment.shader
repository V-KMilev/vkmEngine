/**
 * IBL bake - GGX prefiltered specular, one roughness per mip.
 *
 * Importance-samples the environment with GGX; u_roughness runs 0 at mip 0 to 1 at the last mip.
 */
in vec3 vLocalPos;

out vec4 FragColor;

layout(binding = BAKE_SLOT_SOURCE) uniform samplerCube u_envCube;
uniform float u_roughness;

#include "../../constants.glsl"
#include "../../sampling.glsl"
#include "../../brdf.glsl"

const uint SAMPLE_COUNT = 1024u;

void main() {
    vec3 N = normalize(vLocalPos);
    vec3 V = N;  // split-sum approximation: view = reflection = normal

    // A mirror's every sample would be L = N at level 0; mip 0 is three quarters of the bake, so it
    // takes that one read rather than a thousand.
    if (u_roughness <= 0.0) {
        FragColor = vec4(textureLod(u_envCube, N, 0.0).rgb, 1.0);
        return;
    }

    // Solid angle of one env-cube texel, for Karis mip selection.
    float envRes = float(textureSize(u_envCube, 0).x);
    float saTexel = 4.0 * PI / (6.0 * envRes * envRes);

    vec3  prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        vec3 H  = importanceSampleGGX(xi, N, u_roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            // Karis prefiltered importance sampling: a low pdf reads a coarser mip, so the sun
            // does not alias into fireflies on rough metal.
            float NdotH    = max(dot(N, H), 0.0);
            // distributionGGX takes the GGX alpha (roughness^2).
            float D        = distributionGGX(NdotH, u_roughness * u_roughness);
            // pdf of L is D * NdotH / (4 * VdotH); V is N here, so it is D / 4.
            float pdf      = D * 0.25 + 1e-4;
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 1e-4);
            float mip      = 0.5 * log2(saSample / saTexel);

            prefiltered += textureLod(u_envCube, L, max(mip, 0.0)).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefiltered /= max(totalWeight, 0.001);
    FragColor = vec4(prefiltered, 1.0);
}
