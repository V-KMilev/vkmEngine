/**
 * Ground-Truth Ambient Occlusion (Jimenez et al. 2016), in view space from the
 * G-buffer normal and the prefiltered depth mips. Per slice it sweeps horizons
 * both ways, each step at the mip its pixel length picks (see
 * prefilter/compute.shader), and integrates the cosine-weighted visible arc.
 * Writes the raw integral (visibility.glsl) and bent normal; denoise/compute.shader
 * averages and shapes it.
 *
 * Drawn at half the scene's size: each pixel stands for the scene texel at the corner of its 2x2,
 * whose depth, normal and place it takes, and upsample/compute.shader widens the result.
 */

out vec4 FragColor;  // r = encoded visibility, gb = octahedral bent normal (view space)

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
#include "../basis.glsl"
#include "../depth.glsl"
#include "../noise.glsl"
#include "../camera.glsl"

#include "../scene_gbuffer.glsl"
#include "visibility.glsl"

// Closed-form cosine-weighted visibility between V and a horizon at signed
// angle h, for a normal at signed angle n (both in the slice plane, from V).
float arc(float h, float n) {
    return 0.25 * (-cos(2.0 * h - n) + cos(n) + 2.0 * h * sin(n));
}

// Screen UV + linear view depth -> view-space position, for a symmetric projection; p00 / p11 are
// projection[0][0] / projection[1][1].
vec3 viewPosFromLinearDepth(vec2 uv, float z, float p00, float p11, bool perspective) {
    vec2 spread = perspective ? vec2(z / p00, z / p11) : vec2(1.0 / p00, 1.0 / p11);
    return vec3((uv * 2.0 - 1.0) * spread, -z);
}

// A view-space point's screen UV: viewPosFromLinearDepth's inverse.
vec2 viewToUV(vec3 p, float p00, float p11, bool perspective) {
    vec2 ndc = perspective ? vec2(p00, p11) * p.xy / max(-p.z, 1e-4) : vec2(p00, p11) * p.xy;
    return ndc * 0.5 + 0.5;
}

// An occluder's weight by its distance: whole to 0.385 of the radius, then linearly to none at
// it (XeGTAO's falloff), so mid-range occluders count fully.
float gtaoFalloff(float len) {
    const float FALLOFF_START = 0.385;
    return clamp((u_radius - len) / (u_radius * (1.0 - FALLOFF_START)), 0.0, 1.0);
}

void main() {
    // The projection's scales, for viewToUV and viewPosFromLinearDepth.
    float proj00      = u_camera.projection[0][0];
    float proj11      = u_camera.projection[1][1];
    bool  perspective = cameraIsPerspective();

    const ivec2 texel = ivec2(gl_FragCoord.xy) * 2;
    const vec2  uv    = (vec2(texel) + 0.5) / vec2(textureSize(u_depthMips, 0));
    float z = texelFetch(u_depthMips, texel, 0).r;
    // The sky: nothing to occlude.
    if (z >= SKY_DEPTH) { FragColor = vec4(encodeVisibility(1.0), 0.0, 0.0, 1.0); return; }

    vec3 P = viewPosFromLinearDepth(uv, z, proj00, proj11, perspective);
    vec3 N = sceneNormalAt(texel);
    vec3 V = perspective ? normalize(-P) : vec3(0.0, 0.0, 1.0);

    // Slices turn evenly about V itself, not about the screen's axis: off the centre of a
    // perspective view, directions even on the screen bunch about V, and an open wall at a wide
    // view's edge would read occluded. Two vectors across V carry them.
    vec3 across1, across2;
    basisAcross(V, across1, across2);

    float noise      = effectNoise(gl_FragCoord.xy, NOISE_GTAO);
    float visibility = 0.0;
    vec3  bent       = vec3(0.0);  // average unoccluded direction, accumulated per slice

    for (int s = 0; s < SLICES; ++s) {
        float phi  = (float(s) + noise) * (PI / float(SLICES));
        vec3  dir3 = across1 * cos(phi) + across2 * sin(phi);

        // The slice's screen line: where the radius across V lands, capped at a quarter of the
        // frame's height. A radius reaching past the eye's plane projects back the wrong way, so the
        // line then runs the way a short step does, at the cap.
        const float cap      = 0.25 * u_camera.viewport.y;
        const vec3  reached  = P + dir3 * u_radius;
        const bool  pastEye  = perspective && reached.z > -1e-5;
        const vec3  aim      = pastEye ? P + dir3 * (0.01 * -P.z) : reached;
        vec2        radiusUV = viewToUV(aim, proj00, proj11, perspective) - uv;
        const float reach    = length(radiusUV * u_camera.viewport);
        if (reach < 1e-6 || (!pastEye && reach < 1e-3)) continue;
        radiusUV *= pastEye ? cap / reach : min(1.0, cap / reach);

        // Slice plane = span(V, dir3); n is the projected normal's signed angle from V.
        vec3 sliceN = normalize(cross(dir3, V));

        vec3  projN    = N - sliceN * dot(N, sliceN);
        float projNLen = length(projN);
        if (projNLen < 1e-4) continue;

        float sgn      = sign(dot(dir3, projN));
        float n        = sgn * acos(clamp(dot(projN, V) / projNLen, -1.0, 1.0));

        // Horizon search: keep the highest cos (smallest angle to V) per side, from the
        // hemisphere's edge, which a faded occluder falls back to (XeGTAO's low horizon).
        float low1      = cos(n - 0.5 * PI);
        float low2      = cos(n + 0.5 * PI);
        float cHorizon1 = low1;  // -dir side
        float cHorizon2 = low2;  // +dir side
        for (int t = 1; t <= STEPS; ++t) {
            float st  = (float(t) - 0.5 * noise) / float(STEPS);
            vec2  off = radiusUV * st;

            // Far steps read coarse levels so fetches stay local; the constant
            // puts the first coarser level at about ten pixels.
            float mip = clamp(log2(length(off * u_camera.viewport)) - 3.3, 0.0, u_maxMip);

            vec2 uvP = uv + off;
            if (all(greaterThanEqual(uvP, vec2(0.0))) && all(lessThanEqual(uvP, vec2(1.0)))) {
                float zP   = textureLod(u_depthMips, uvP, mip).r;
                vec3  sh   = viewPosFromLinearDepth(uvP, zP, proj00, proj11, perspective) - P;
                float len  = length(sh);
                float c    = dot(sh, V) / max(len, 1e-4) - HORIZON_BIAS;
                float fall = gtaoFalloff(len);
                cHorizon2  = max(cHorizon2, mix(low2, c, fall));
            }
            vec2 uvN = uv - off;
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
        bent += projNLen * (V * cos(bentAngle) + dir3 * sin(bentAngle));
    }

    visibility /= float(SLICES);

    // Fall back to the geometric normal where every slice was degenerate.
    vec3 bentN = (dot(bent, bent) > 1e-8) ? normalize(bent) : N;

    FragColor = vec4(encodeVisibility(visibility), octEncode(bentN), 1.0);
}
