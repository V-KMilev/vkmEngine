/**
 * Screen-space reflections - view-space ray march over the prepass G-buffer.
 *
 * Reuses the GTAO G-buffer (view normal + view position) and the resolved
 * HDR scene color. Marches the reflection ray in view space, projecting each
 * step to screen to test against stored geometry, then binary-refines the
 * hit. Output is additively blended into the HDR target (composite tone-maps
 * it with the scene). Per-material: Fresnel F0 from packed metalness, and a
 * roughness fade (no roughness-mip blur yet, so rough surfaces drop the
 * sharp screen reflection rather than smearing it).
 */
#version 420 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D u_sceneColor;
uniform sampler2D u_viewNormal;
uniform sampler2D u_viewPos;

uniform mat4  u_projection;
uniform float u_intensity;
uniform float u_maxDistance;
uniform float u_thickness;

const int LINEAR_STEPS = 32;
const int BINARY_STEPS = 5;

bool isBackground(vec3 p) {
    return dot(p, p) < 1e-8;  // prepass clears view-pos to 0
}

bool toUV(vec3 viewPos, out vec2 uv) {
    vec4 clip = u_projection * vec4(viewPos, 1.0);
    if (clip.w <= 0.0) return false;
    uv = (clip.xy / clip.w) * 0.5 + 0.5;
    return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
}

void main() {
    vec3 P = texture(u_viewPos, vUV).xyz;
    if (isBackground(P)) {
        FragColor = vec4(0.0);
        return;
    }

    vec3 N = normalize(texture(u_viewNormal, vUV).xyz);
    vec3 V = normalize(-P);
    vec3 R = normalize(reflect(-V, N));

    // Reflections going back toward the camera rarely have screen data.
    if (R.z > 0.0) {
        FragColor = vec4(0.0);
        return;
    }

    float stepLen = u_maxDistance / float(LINEAR_STEPS);
    vec3  ray = P;
    vec2  hitUV = vec2(0.0);
    bool  hit = false;

    for (int i = 0; i < LINEAR_STEPS; ++i) {
        ray += R * stepLen;

        vec2 uv;
        if (!toUV(ray, uv)) break;

        vec3 scene = texture(u_viewPos, uv).xyz;
        if (isBackground(scene)) continue;

        float dz = scene.z - ray.z;          // >0 => ray went behind surface
        if (dz > 0.0 && dz < u_thickness) {
            // Binary refine between the last two samples.
            vec3 lo = ray - R * stepLen;
            vec3 hi = ray;
            for (int b = 0; b < BINARY_STEPS; ++b) {
                vec3 mid = (lo + hi) * 0.5;
                vec2 muv;
                if (!toUV(mid, muv)) break;
                vec3 ms = texture(u_viewPos, muv).xyz;
                if (!isBackground(ms) && (ms.z - mid.z) > 0.0) hi = mid;
                else lo = mid;
                hitUV = muv;
            }
            hit = true;
            break;
        }
    }

    if (!hit) {
        FragColor = vec4(0.0);
        return;
    }

    // Per-material reflectivity: roughness in normal.a, metalness in pos.a
    // (packed by the prepass). Metals reflect strongly (F0 -> 1), dielectrics
    // ~4%; rougher surfaces lose the (unblurred) screen reflection.
    float roughness = clamp(texture(u_viewNormal, vUV).a, 0.0, 1.0);
    float metalness = clamp(texture(u_viewPos,    vUV).a, 0.0, 1.0);

    float NdotV = max(dot(N, V), 0.0);
    float F0 = mix(0.04, 1.0, metalness);
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
    float roughFade = 1.0 - smoothstep(0.25, 0.75, roughness);

    // Fade near screen borders to hide the screen-space cutoff.
    float edge = min(min(hitUV.x, 1.0 - hitUV.x), min(hitUV.y, 1.0 - hitUV.y));
    float edgeFade = smoothstep(0.0, 0.08, edge);

    vec3 reflected = texture(u_sceneColor, hitUV).rgb;
    vec3 result = reflected * fresnel * roughFade * u_intensity * edgeFade;

    FragColor = vec4(max(result, vec3(0.0)), 1.0);
}
