/**
 * IBL bake - GGX prefiltered specular, one roughness per mip.
 *
 * Importance-samples the environment cubemap with the GGX distribution. The
 * bake pass sets u_roughness per mip level (0 at mip 0 .. 1 at the last mip).
 */

in vec3 vLocalPos;

out vec4 FragColor;

uniform samplerCube u_envCube;
uniform float u_roughness;

#include "../../_common/constants.glsl"
#include "../../_common/sampling.glsl"
#include "../../_common/brdf.glsl"
const uint  SAMPLE_COUNT = 1024u;

void main() {
    vec3 N = normalize(vLocalPos);
    vec3 V = N;  // split-sum approximation: view = reflection = normal

    // Solid angle of one env-cube texel (used for Karis mip selection).
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
            // Karis "prefiltered importance sampling": sample a coarser env
            // mip when the GGX pdf is low, so bright pixels (the sun) do not
            // alias into fireflies/sparkle on rough metal.
            float NdotH    = max(dot(N, H), 0.0);
            // The shared distributionGGX takes the GGX alpha (roughness^2).
            float D        = distributionGGX(NdotH, u_roughness * u_roughness);
            float pdf      = (D * NdotH / (4.0 * NdotH)) + 1e-4;
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 1e-4);
            float mip      = (u_roughness < 1e-3)
                           ? 0.0 : 0.5 * log2(saSample / saTexel);

            prefiltered += textureLod(u_envCube, L, max(mip, 0.0)).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefiltered /= max(totalWeight, 0.001);
    FragColor = vec4(prefiltered, 1.0);
}
