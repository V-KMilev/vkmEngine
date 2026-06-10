/**
 * Screen-space reflections (view-space ray-march, G-buffer driven).
 *
 * Reads the scene's depth + G-buffer (view normal, roughness, metalness),
 * reflects the view ray and marches it against the depth buffer; on a hit it
 * samples the scene colour. The reflection is weighted by Fresnel (F0 from
 * metalness, so mirrors reflect at full strength) and faded out with roughness
 * and near screen edges. Output = scene colour + reflection (this pass renders
 * into the scratch target and is blitted back, so it never reads a bound
 * attachment).
 */
#version 430 core

in vec2 vUV;

out vec4 FragColor;

layout(binding = 18) uniform sampler2D u_sceneColor;  // live scene colour
layout(binding = 19) uniform sampler2D u_depth;       // scene depth
layout(binding = 20) uniform sampler2D u_gbuffer;     // oct view-normal.xy, roughness, metalness

uniform mat4 u_projection;
uniform mat4 u_invProjection;

const int   STEPS        = 48;
const int   REFINE       = 5;
const float MAX_DISTANCE = 30.0;   // view-space units
const float THICKNESS    = 2.0;    // reject crossings deeper than this (view-space)
const float MAX_ROUGH    = 0.6;    // surfaces rougher than this don't get SSR
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

vec3 toUV(vec3 viewP) {
    vec4 clip = u_projection * vec4(viewP, 1.0);
    vec3 ndc  = clip.xyz / clip.w;
    return vec3(ndc.xy * 0.5 + 0.5, ndc.z * 0.5 + 0.5);
}

void main() {
    vec3  scene     = texture(u_sceneColor, vUV).rgb;  // passthrough
    float depth     = texture(u_depth, vUV).r;
    vec4  g         = texture(u_gbuffer, vUV);
    float roughness = g.b;
    float metalness = g.a;

    // Sky or surfaces too rough to show a sharp reflection: pass through.
    if (depth >= 1.0 || roughness > MAX_ROUGH) { FragColor = vec4(scene, 1.0); return; }

    vec3 P = viewPos(vUV, depth);
    vec3 N = octDecode(g.rg);
    vec3 V = normalize(-P);
    vec3 R = reflect(normalize(P), N);

    float stepLen = MAX_DISTANCE / float(STEPS);
    vec3  ray     = P;
    vec2  hitUV   = vec2(0.0);
    bool  hit     = false;

    for (int i = 0; i < STEPS; ++i) {
        ray += R * stepLen;
        vec3 suv = toUV(ray);
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;

        float sd = texture(u_depth, suv.xy).r;
        if (sd >= 1.0) continue;  // sky: nothing to hit here

        vec3 sp = viewPos(suv.xy, sd);
        // Ray went behind the sampled surface => it crossed it.
        if (sp.z - ray.z > 0.0) {
            vec3 lo = ray - R * stepLen;
            vec3 hi = ray;
            for (int b = 0; b < REFINE; ++b) {
                vec3 mid = (lo + hi) * 0.5;
                vec3 muv = toUV(mid);
                vec3 mp  = viewPos(muv.xy, texture(u_depth, muv.xy).r);
                if (mp.z - mid.z > 0.0) hi = mid; else lo = mid;
                hitUV = muv.xy;
            }
            // Reject if the surface at the refined hit is far behind the ray
            // (the ray skimmed past a thin object into the background).
            vec3 mid = (lo + hi) * 0.5;
            float fd = viewPos(hitUV, texture(u_depth, hitUV).r).z - mid.z;
            if (abs(fd) < THICKNESS) hit = true;
            break;
        }
    }

    vec3 refl = vec3(0.0);
    if (hit) {
        float NdotV = max(dot(N, V), 0.0);
        vec3  F0    = mix(vec3(0.04), vec3(1.0), metalness);   // metals reflect strongly
        vec3  F     = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
        float roughFade = 1.0 - smoothstep(0.0, MAX_ROUGH, roughness);
        float edge = min(min(hitUV.x, 1.0 - hitUV.x), min(hitUV.y, 1.0 - hitUV.y));
        float edgeFade = smoothstep(0.0, 0.1, edge);

        refl = texture(u_sceneColor, hitUV).rgb * F * (roughFade * edgeFade * INTENSITY);
    }

    FragColor = vec4(scene + refl, 1.0);
}
