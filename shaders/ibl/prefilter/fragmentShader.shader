/**
 * IBL bake - GGX prefiltered specular, one roughness per mip.
 *
 * Importance-samples the environment cubemap with the GGX distribution. The
 * bake pass sets u_roughness per mip level (0 at mip 0 .. 1 at the last mip).
 */
#version 420 core

in vec3 vLocalPos;

out vec4 FragColor;

uniform samplerCube u_envCube;
uniform float u_roughness;

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

void main() {
    vec3 N = normalize(vLocalPos);
    vec3 V = N;  // split-sum approximation: view = reflection = normal

    vec3  prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        vec3 H  = importanceSampleGGX(xi, N, u_roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefiltered += texture(u_envCube, L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefiltered /= max(totalWeight, 0.001);
    FragColor = vec4(prefiltered, 1.0);
}
