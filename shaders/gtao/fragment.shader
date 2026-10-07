/**
 * Ground-Truth Ambient Occlusion (Jimenez et al. 2016), in view space from the
 * G-buffer normal and the prefiltered depth mips. Per slice it sweeps horizons
 * both ways, each step at the mip its pixel length picks (see
 * prefilter/compute.shader), and integrates the cosine-weighted visible arc.
 * Writes the raw integral (visibility.glsl) and bent normal; denoise/compute.shader
 * averages and shapes it.
 */
in vec2 vUV;

out vec4 FragColor;  // r = encoded visibility, gb = octahedral bent normal (view space)

// Oct view-normal, roughness, metalness.
layout(binding = POST_SLOT_SCENE_GBUFFER) uniform sampler2D u_sceneGBuffer;
// Linear view depth, as a mip chain.
layout(binding = POST_SLOT_AO_DEPTH)      uniform sampler2D u_depthMips;

uniform float u_maxMip;  // the chain's last level
uniform float u_radius;  // world-space sample radius

// Few, because the denoise averages 25 pixels' worth of them.
const int SLICES = 2;
const int STEPS  = 4;

// Taken off each horizon cosine so a flat surface's own depth steps (quantised
// depth, coarse mips) never self-occlude. A guard, not a look: not a setting.
const float HORIZON_BIAS = 0.03;

#include "../constants.glsl"
#include "depth_mips.glsl"
#include "../noise.glsl"
#include "../camera.glsl"

#include "../normal_codec.glsl"
#include "visibility.glsl"

// Closed-form cosine-weighted visibility between V and a horizon at signed
// angle h, for a normal at signed angle n (both in the slice plane, from V).
float arc(float h, float n) {
    return 0.25 * (-cos(2.0 * h - n) + cos(n) + 2.0 * h * sin(n));
}

// An occluder's weight by its distance: whole to 0.385 of the radius, then linearly to none at
// it (XeGTAO's falloff), so mid-range occluders count fully.
float gtaoFalloff(float len) {
    const float FALLOFF_START = 0.385;
    return clamp((u_radius - len) / (u_radius * (1.0 - FALLOFF_START)), 0.0, 1.0);
}

void main() {
    // projection[0][0] takes a view x to the screen, [1][1] a world radius.
    float proj00      = u_camera.projection[0][0];
    float proj11      = u_camera.projection[1][1];
    bool  perspective = cameraIsPerspective();

    float z = texelFetch(u_depthMips, ivec2(gl_FragCoord.xy), 0).r;
    // The sky: nothing to occlude.
    if (z >= SKY_DEPTH) { FragColor = vec4(encodeVisibility(1.0), 0.0, 0.0, 1.0); return; }

    vec3 P = viewPosFromLinearDepth(vUV, z, proj00, proj11, perspective);
    vec3 N = octDecode(texture(u_sceneGBuffer, vUV).rg);
    vec3 V = perspective ? normalize(-P) : vec3(0.0, 0.0, 1.0);

    // World radius -> per-axis UV radius, true at any aspect. The cap keeps a
    // surface at the lens from sampling the whole screen.
    float depthScale   = perspective ? max(-P.z, 1e-3) : 1.0;
    float radiusUV     = min(u_radius * proj11 / (2.0 * depthScale), 0.25);
    vec2  radiusAxisUV = radiusUV * vec2(proj00 / proj11, 1.0);

    float noise      = effectNoise(gl_FragCoord.xy, NOISE_GTAO);
    float visibility = 0.0;
    vec3  bent       = vec3(0.0);  // average unoccluded direction, accumulated per slice

    for (int s = 0; s < SLICES; ++s) {
        float phi = (float(s) + noise) * (PI / float(SLICES));
        vec2  dir = vec2(cos(phi), sin(phi));

        // Slice plane = span(V, dir); n is the projected normal's signed angle from V.
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

        // Horizon search: keep the highest cos (smallest angle to V) per side, from the
        // hemisphere's edge, which a faded occluder falls back to (XeGTAO's low horizon).
        float low1      = cos(n - 0.5 * PI);
        float low2      = cos(n + 0.5 * PI);
        float cHorizon1 = low1;  // -dir side
        float cHorizon2 = low2;  // +dir side
        for (int t = 1; t <= STEPS; ++t) {
            float st  = (float(t) - 0.5 * noise) / float(STEPS);
            vec2  off = dir * radiusAxisUV * st;

            // Far steps read coarse levels so fetches stay local; the constant
            // puts the first coarser level at about ten pixels.
            float mip = clamp(log2(length(off * u_camera.viewport)) - 3.3, 0.0, u_maxMip);

            vec2 uvP = vUV + off;
            if (all(greaterThanEqual(uvP, vec2(0.0))) && all(lessThanEqual(uvP, vec2(1.0)))) {
                float zP   = textureLod(u_depthMips, uvP, mip).r;
                vec3  sh   = viewPosFromLinearDepth(uvP, zP, proj00, proj11, perspective) - P;
                float len  = length(sh);
                float c    = dot(sh, V) / max(len, 1e-4) - HORIZON_BIAS;
                float fall = gtaoFalloff(len);
                cHorizon2  = max(cHorizon2, mix(low2, c, fall));
            }
            vec2 uvN = vUV - off;
            if (all(greaterThanEqual(uvN, vec2(0.0))) && all(lessThanEqual(uvN, vec2(1.0)))) {
                float zN   = textureLod(u_depthMips, uvN, mip).r;
                vec3  sh   = viewPosFromLinearDepth(uvN, zN, proj00, proj11, perspective) - P;
                float len  = length(sh);
                float c    = dot(sh, V) / max(len, 1e-4) - HORIZON_BIAS;
                float fall = gtaoFalloff(len);
                cHorizon1  = max(cHorizon1, mix(low1, c, fall));
            }
        }

        // Horizon cosines to signed angles, clamped to the normal's hemisphere.
        float h1 = n + max(-acos(clamp(cHorizon1, -1.0, 1.0)) - n, -0.5 * PI);
        float h2 = n + min( acos(clamp(cHorizon2, -1.0, 1.0)) - n,  0.5 * PI);
        visibility += projNLen * (arc(h1, n) + arc(h2, n));

        // The visible arc's bisector, weighted like the visibility.
        float bentAngle = (h1 + h2) * 0.5;
        bent += projNLen * (V * cos(bentAngle) + ortho * sin(bentAngle));
    }

    visibility /= float(SLICES);

    // Fall back to the geometric normal where every slice was degenerate.
    vec3 bentN = (dot(bent, bent) > 1e-8) ? normalize(bent) : N;

    FragColor = vec4(encodeVisibility(visibility), octEncode(bentN), 1.0);
}
