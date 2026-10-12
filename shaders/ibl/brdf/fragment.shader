/**
 * IBL bake - split-sum BRDF/DFG integration LUT.
 *
 * x = N.V, y = roughness; .rg is the (scale, bias) on the prefiltered specular: F * dfg.x + dfg.y.
 * .b is the sheen lobe's directional albedo at that roughness (KHR_materials_sheen's E), what a
 * sheen takes of the light, so the layer under it can be dimmed by as much.
 */
in vec2 vUV;

out vec4 FragColor;  // RGBA: GL requires RGBA16F renderable, not RGB16F

#include "../../brdf.glsl"
#include "../../sampling.glsl"

const uint SAMPLE_COUNT = 1024u;

// The Charlie lobe is broad enough for samples spread evenly over the hemisphere.
const uint SHEEN_SAMPLES = 256u;

// The integral of the sheen lobe's D V cos over the hemisphere, as the forward pass shades it.
float sheenAlbedo(vec3 V, float NdotV, float roughness) {
    float alpha = roughness * roughness;
    float sum   = 0.0;
    for (uint i = 0u; i < SHEEN_SAMPLES; ++i) {
        vec2  xi    = hammersley(i, SHEEN_SAMPLES);
        float cosT  = xi.x;
        float sinT  = sqrt(1.0 - cosT * cosT);
        float phi   = 2.0 * PI * xi.y;
        vec3  L     = vec3(sinT * cos(phi), sinT * sin(phi), cosT);
        vec3  H     = normalize(V + L);
        float NdotL = L.z;
        sum += distributionCharlie(H.z, alpha) * visCharlie(NdotV, NdotL, alpha) * NdotL;
    }
    return sum * 2.0 * PI / float(SHEEN_SAMPLES);
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
            // The direct lights' visibility term, so both integrate the same lobe; pdf D*NoH / (4 VoH).
            float Vis  = visSmithCorrelated(NdotV, NdotL, roughness * roughness);
            float gVis = 4.0 * Vis * NdotL * VdotH / max(NdotH, 1e-4);
            float Fc   = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * gVis;
            B += Fc * gVis;
        }
    }

    FragColor = vec4(vec2(A, B) / float(SAMPLE_COUNT), sheenAlbedo(V, NdotV, roughness), 1.0);
}
