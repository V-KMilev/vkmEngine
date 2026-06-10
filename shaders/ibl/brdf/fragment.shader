/**
 * IBL bake - split-sum BRDF/DFG integration LUT.
 *
 * x axis = N.V, y axis = roughness. Output .rg is the (scale, bias) the
 * forward shader applies to the prefiltered specular: F * dfg.x + dfg.y.
 * Rendered once into an RG16F texture.
 */
#version 420 core

in vec2 vUV;

out vec2 FragColor;

const float PI = 3.14159265359;
const uint  SAMPLE_COUNT = 1024u;

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

vec3 importanceSampleGGX(vec2 xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    vec3 up        = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * h.x + bitangent * h.y + N * h.z);
}

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
