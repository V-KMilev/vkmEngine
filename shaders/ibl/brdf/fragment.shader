/**
 * IBL bake - split-sum BRDF/DFG integration LUT.
 *
 * x axis = N.V, y axis = roughness. Output .rg is the (scale, bias) the
 * forward shader applies to the prefiltered specular: F * dfg.x + dfg.y.
 * Rendered once into an RG16F texture.
 */

in vec2 vUV;

out vec2 FragColor;

#include "../../_common/constants.glsl"
#include "../../_common/sampling.glsl"
const uint  SAMPLE_COUNT = 1024u;

// Smith geometry with the IBL k = a^2 / 2.
float geometrySmithIBL(float NdotV, float NdotL, float roughness) {
    float k = (roughness * roughness) / 2.0;
    float ggxV = NdotV / (NdotV * (1.0 - k) + k);
    float ggxL = NdotL / (NdotL * (1.0 - k) + k);
    return ggxV * ggxL;
}

void main() {
    float NdotV = max(vUV.x, 1e-4);
    float roughness = vUV.y;

    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    vec3 N = vec3(0.0, 0.0, 1.0);

    float A = 0.0;
    float B = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        vec3 H  = importanceSampleGGX(xi, N, roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            float G = geometrySmithIBL(NdotV, NdotL, roughness);
            float gVis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * gVis;
            B += Fc * gVis;
        }
    }

    FragColor = vec2(A, B) / float(SAMPLE_COUNT);
}
