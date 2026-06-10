/**
 * Ground-Truth Ambient Occlusion (Jimenez et al. 2016, horizon-slice integral).
 *
 * Reads the opaque depth + G-buffer (octahedral view-normal) the depth prepass
 * laid down, reconstructs view-space position, and for a handful of slices
 * sweeps screen-space horizons to both sides. Per slice it integrates the
 * cosine-weighted visibility arc between the two horizons (the closed-form GTAO
 * integral), so unlike a plain occlusion count the result is the actual
 * normal-weighted AO. Output is a single factor in [0,1] (1 = unoccluded) the
 * forward pass multiplies into the ambient/IBL term.
 *
 * Works entirely in view space from depth + the G-buffer normal - no world-space
 * data needed. Interleaved-gradient rotation per pixel hides slice banding.
 */
#version 430 core

in vec2 vUV;

out float FragColor;

layout(binding = 19) uniform sampler2D u_depth;     // scene depth
layout(binding = 20) uniform sampler2D u_gbuffer;   // oct view-normal.xy, roughness, metalness

uniform mat4  u_invProjection;
uniform float u_proj11;     // projection[1][1]: world radius -> screen
uniform float u_radius;     // world-space sample radius
uniform float u_intensity;  // occlusion strength
uniform float u_power;      // contrast curve
uniform float u_bias;       // view-space self-occlusion guard

const int   SLICES  = 3;
const int   STEPS   = 5;
const float PI      = 3.14159265359;
const float HALF_PI = 1.57079632679;

vec2 signNotZero(vec2 v) {
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec3 octDecode(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * signNotZero(n.xy);
    return normalize(n);
}

vec3 viewPos(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 v   = u_invProjection * ndc;
    return v.xyz / v.w;
}

float interleavedGradient(vec2 p) {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

// Closed-form inner integral of cosine-weighted visibility between the view
// vector and a horizon at signed angle h, for a normal at signed angle n
// (both measured in the slice plane, relative to V).
float arc(float h, float n) {
    return 0.25 * (-cos(2.0 * h - n) + cos(n) + 2.0 * h * sin(n));
}

void main() {
    float depth = texture(u_depth, vUV).r;
    if (depth >= 1.0) { FragColor = 1.0; return; }   // sky: nothing to occlude

    vec3 P = viewPos(vUV, depth);
    vec3 N = octDecode(texture(u_gbuffer, vUV).rg);
    vec3 V = normalize(-P);

    // World radius -> screen-space UV radius at this depth; cap the near-camera
    // blowup so a surface right at the lens does not sample the whole screen.
    float radiusUV = min(u_radius * u_proj11 / (2.0 * max(-P.z, 1e-3)), 0.25);

    float noise = interleavedGradient(gl_FragCoord.xy);
    float visibility = 0.0;

    for (int s = 0; s < SLICES; ++s) {
        float phi = (float(s) + noise) * (PI / float(SLICES));
        vec2  dir = vec2(cos(phi), sin(phi));

        // Slice plane = span(V, dir). Project the normal into it and find its
        // signed angle n relative to V.
        vec3  dir3     = vec3(dir, 0.0);
        vec3  sliceN   = cross(dir3, V);
        float sliceLen = length(sliceN);
        if (sliceLen < 1e-5) continue;
        sliceN /= sliceLen;

        vec3  projN    = N - sliceN * dot(N, sliceN);
        float projNLen = length(projN);
        if (projNLen < 1e-4) continue;

        vec3  ortho    = normalize(dir3 - V * dot(dir3, V));
        float sgn      = sign(dot(ortho, projN));
        float n        = sgn * acos(clamp(dot(projN, V) / projNLen, -1.0, 1.0));

        // Horizon search: keep the highest cos (smallest angle to V) per side.
        float cHorizon1 = -1.0;  // -dir side
        float cHorizon2 = -1.0;  // +dir side
        for (int t = 1; t <= STEPS; ++t) {
            float st  = (float(t) - 0.5 * noise) / float(STEPS);
            vec2  off = dir * radiusUV * st;

            vec2 uvP = vUV + off;
            if (all(greaterThanEqual(uvP, vec2(0.0))) && all(lessThanEqual(uvP, vec2(1.0)))) {
                vec3  sh   = viewPos(uvP, texture(u_depth, uvP).r) - P;
                float len  = length(sh);
                float c    = dot(sh, V) / max(len, 1e-4) - u_bias;
                float fall = clamp(1.0 - len / u_radius, 0.0, 1.0);   // distant occluders fade out
                cHorizon2  = max(cHorizon2, mix(-1.0, c, fall));
            }
            vec2 uvN = vUV - off;
            if (all(greaterThanEqual(uvN, vec2(0.0))) && all(lessThanEqual(uvN, vec2(1.0)))) {
                vec3  sh   = viewPos(uvN, texture(u_depth, uvN).r) - P;
                float len  = length(sh);
                float c    = dot(sh, V) / max(len, 1e-4) - u_bias;
                float fall = clamp(1.0 - len / u_radius, 0.0, 1.0);
                cHorizon1  = max(cHorizon1, mix(-1.0, c, fall));
            }
        }

        // Convert horizon cosines to signed angles, clamp to the normal's
        // hemisphere, and accumulate the cosine-weighted arc.
        float h1 = n + max(-acos(clamp(cHorizon1, -1.0, 1.0)) - n, -HALF_PI);
        float h2 = n + min( acos(clamp(cHorizon2, -1.0, 1.0)) - n,  HALF_PI);
        visibility += projNLen * (arc(h1, n) + arc(h2, n));
    }

    visibility /= float(SLICES);

    // visibility >= 1 on open surfaces -> AO clamps to 1 (no false darkening);
    // occluders pull it down. Intensity scales the occluded part, power adds bite.
    float ao = clamp(1.0 - u_intensity * (1.0 - visibility), 0.0, 1.0);
    FragColor = pow(ao, u_power);
}
