/**
 * Screen-space reflections (per-pixel DDA, McGuire-style).
 *
 * Reflects the view ray off the G-buffer normal and traces it in SCREEN space
 * one pixel at a time along the ray's major axis (a DDA line walk), comparing a
 * perspective-correct interpolation of the ray's depth against the depth buffer
 * inside a thickness slab. Per-pixel stepping never skips or oversamples, which
 * removes the banding / "lines" that equal view-space steps produce (they
 * project to wildly uneven screen distances). On a hit it samples the scene
 * colour, Fresnel- and roughness-weighted and faded near screen edges. Renders
 * into the scratch target and is blitted back, so it never reads a bound
 * attachment.
 *
 * Ref: McGuire & Mara, "Efficient GPU Screen-Space Ray Tracing", JCGT 2014.
 */
#version 430 core

in vec2 vUV;

out vec4 FragColor;

layout(binding = 18) uniform sampler2D u_sceneColor;  // live scene colour
layout(binding = 19) uniform sampler2D u_depth;       // scene depth
layout(binding = 20) uniform sampler2D u_gbuffer;     // oct view-normal.xy, roughness, metalness

uniform mat4 u_projection;
uniform mat4 u_invProjection;

const float STRIDE       = 2.0;    // pixels advanced per step (1 = exact, >1 = faster)
const int   MAX_STEPS    = 256;    // hard cap on the DDA walk
const float MAX_DISTANCE = 30.0;   // view-space ray length
const float NEAR_Z       = 0.05;   // keep the ray end in front of the camera
const float THICKNESS    = 1.2;    // view-space surface thickness for the crossing slab
const float START_BIAS   = 0.05;   // lift the ray off its own surface (anti-acne)
const float MAX_ROUGH    = 0.6;    // rougher surfaces don't get a sharp reflection
const float INTENSITY    = 1.0;

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

// Interleaved-gradient noise: jitters the start so any residual stepping reads
// as fine dither, not a coherent edge.
float ign(vec2 p) {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

void main() {
    vec3  scene     = texture(u_sceneColor, vUV).rgb;  // passthrough base
    float depth0    = texture(u_depth, vUV).r;
    vec4  g         = texture(u_gbuffer, vUV);
    float roughness = g.b;
    float metalness = g.a;

    // Sky, or too rough for a sharp reflection: pass through unchanged.
    if (depth0 >= 1.0 || roughness > MAX_ROUGH) { FragColor = vec4(scene, 1.0); return; }

    vec2 texSize = vec2(textureSize(u_depth, 0));

    vec3 P = viewPos(vUV, depth0);
    vec3 N = octDecode(g.rg);
    vec3 V = normalize(-P);
    vec3 R = reflect(normalize(P), N);

    // Ray origin lifted off the surface (anti-acne); clip the end to the near
    // plane so we never project a point behind the camera.
    vec3  rayOrigin = P + N * START_BIAS;
    float rayLen    = (rayOrigin.z + R.z * MAX_DISTANCE) > -NEAR_Z
                      ? (-NEAR_Z - rayOrigin.z) / R.z
                      : MAX_DISTANCE;
    vec3  rayEnd    = rayOrigin + R * rayLen;

    // Project endpoints to pixel coords. Keep k = 1/w and Q = viewPos * k; both
    // interpolate linearly in screen space, so the ray's view depth at any step
    // is -(Q.z / k) - perspective-correct without a per-step matrix multiply.
    vec4  H0 = u_projection * vec4(rayOrigin, 1.0);
    vec4  H1 = u_projection * vec4(rayEnd,    1.0);
    float k0 = 1.0 / H0.w, k1 = 1.0 / H1.w;
    vec3  Q0 = rayOrigin * k0, Q1 = rayEnd * k1;
    vec2  P0 = (H0.xy * k0 * 0.5 + 0.5) * texSize;
    vec2  P1 = (H1.xy * k1 * 0.5 + 0.5) * texSize;

    // Guard a degenerate sub-pixel line, then choose the major axis to walk.
    P1 += (dot(P1 - P0, P1 - P0) < 0.0001) ? vec2(0.01) : vec2(0.0);
    vec2 delta   = P1 - P0;
    bool permute = false;
    if (abs(delta.x) < abs(delta.y)) { permute = true; delta = delta.yx; P0 = P0.yx; P1 = P1.yx; }
    float stepDir = sign(delta.x);
    float invdx   = stepDir / delta.x;

    // Per-step increments: STRIDE pixels along the major axis, the matching slope
    // on the minor axis, and the linear k / Q deltas.
    vec2  dP = vec2(stepDir, delta.y * invdx) * STRIDE;
    vec3  dQ = (Q1 - Q0) * invdx * STRIDE;
    float dk = (k1 - k0) * invdx * STRIDE;

    float jitter = ign(gl_FragCoord.xy);
    vec2  Pp = P0 + dP * jitter;
    vec3  Qq = Q0 + dQ * jitter;
    float kk = k0 + dk * jitter;

    float dPrev = -(Qq.z / kk);   // ray depth at the (jittered) start, positive
    int   count = int(min(abs(delta.x) / STRIDE, float(MAX_STEPS)));
    vec2  hitUV = vec2(0.0);
    bool  hit   = false;

    for (int i = 0; i < count; ++i) {
        Pp += dP; Qq += dQ; kk += dk;

        vec2 pixel = permute ? Pp.yx : Pp;
        if (pixel.x < 0.0 || pixel.x > texSize.x || pixel.y < 0.0 || pixel.y > texSize.y) break;

        vec2  uv    = pixel / texSize;
        float sd    = texture(u_depth, uv).r;
        float dCurr = -(Qq.z / kk);                   // ray depth here, positive
        if (sd >= 1.0) { dPrev = dCurr; continue; }   // sky: nothing to hit

        float sceneDepth = -viewPos(uv, sd).z;        // scene depth, positive

        // Slab crossing: the step interval [dPrev, dCurr] overlaps the surface's
        // [sceneDepth, sceneDepth + THICKNESS] slab (so the ray entered it this
        // step rather than skimming past a thin object into the background).
        if (dCurr >= sceneDepth && dPrev <= sceneDepth + THICKNESS) {
            hitUV = uv;
            hit   = true;
            break;
        }
        dPrev = dCurr;
    }

    vec3 refl = vec3(0.0);
    if (hit) {
        float NdotV     = max(dot(N, V), 0.0);
        vec3  F0        = mix(vec3(0.04), vec3(1.0), metalness);  // metals reflect strongly
        vec3  F         = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
        float roughFade = 1.0 - smoothstep(0.0, MAX_ROUGH, roughness);
        float edge      = min(min(hitUV.x, 1.0 - hitUV.x), min(hitUV.y, 1.0 - hitUV.y));
        float edgeFade  = smoothstep(0.0, 0.1, edge);

        refl = texture(u_sceneColor, hitUV).rgb * F * (roughFade * edgeFade * INTENSITY);
    }

    FragColor = vec4(scene + refl, 1.0);
}
