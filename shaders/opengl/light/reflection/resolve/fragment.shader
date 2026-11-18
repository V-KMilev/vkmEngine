/**
 * Reflection resolve: the traced color in place of the environment's. The
 * forward pass (forward/pbr/fragment.shader) added weight x environment and
 * wrote that product beside it; this adds (weight x traced - product) x trust.
 *
 * A glossy pixel also reads its neighbors' rays, each only as far as it lies on
 * the same surface - one ray per pixel made a lobe, and the edge of found rays a
 * fade, with no temporal filter.
 */

out vec4 FragColor;

layout(binding = POST_SLOT_SCENE_COLOR)   uniform sampler2D u_sceneColor;
layout(binding = POST_SLOT_SCENE_DEPTH)   uniform sampler2D u_sceneDepth;
layout(binding = REFLECT_SLOT_WEIGHT)     uniform sampler2D u_weight;
layout(binding = REFLECT_SLOT_ENV)        uniform sampler2D u_env;
layout(binding = REFLECT_SLOT_TRACE)      uniform sampler2D u_hits;
layout(binding = REFLECT_SLOT_CHAIN)      uniform sampler2D u_chain;

uniform float u_maxLod;  // the color chain's last level; the trace stored hit levels over it
uniform int   u_traceStep;  // pixels a hit stands for on each axis: 1, or 2 at half resolution

#include "../scene_read.glsl"

// Smoother than this, a pixel traced at full resolution reads its own hit alone: a mirror keeps its
// picture's pixels.
const float MIRROR_ROUGHNESS = 0.09;

// The lit scene at @p lod levels blurred: level 0 is the scene, the chain holds those below.
vec3 chainAt(vec2 uv, float lod) {
    if (lod >= 1.0) return textureLod(u_chain, uv, lod - 1.0).rgb;
    const vec3 sharp = textureLod(u_sceneColor, uv, 0.0).rgb;
    return lod <= 0.0 ? sharp : mix(sharp, textureLod(u_chain, uv, 0.0).rgb, lod);
}

void main() {
    const ivec2 texel = ivec2(gl_FragCoord.xy);
    const vec4  scene = texelFetch(u_sceneColor, texel, 0);
    FragColor = scene;

    const vec4  weight    = texelFetch(u_weight, texel, 0);
    const float roughness = weight.a;
    if (!tracesReflection(weight)) return;
    if (!sceneHasNormal(texel)) return;

    const vec2  size  = vec2(textureSize(u_sceneDepth, 0));
    const ivec2 last  = textureSize(u_hits, 0) - 1;
    const vec3  p     = sceneViewPosAt(texel, size);
    const vec3  n     = sceneNormalAt(texel);
    const vec3  added = texelFetch(u_env, texel, 0).rgb;

    // Traced at half resolution, a mirror gathers too: the hits about it, from the pixels on its
    // own surface, are its upsampling.
    const int   reach = roughness < MIRROR_ROUGHNESS && u_traceStep == 1 ? 0 : 1;
    const ivec2 own   = texel / u_traceStep;

    vec3  traced = vec3(0.0);
    float found  = 0.0;
    float total  = 0.0;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const ivec2 h = clamp(own + ivec2(dx, dy), ivec2(0), last);
            const ivec2 q = h * u_traceStep;  // the pixel the hit was traced from
            float w = exp(-0.5 * float(dx * dx + dy * dy));
            if (q != texel) {
                // Off this pixel's plane, or turned from it: another surface.
                if (!sceneHasNormal(q)) continue;
                const float off = abs(dot(n, sceneViewPosAt(q, size) - p));
                w *= 1.0 - smoothstep(0.005, 0.02, off / -p.z);
                w *= pow(max(dot(sceneNormalAt(q), n), 0.0), 8.0);
            }
            total += w;

            const vec4 hit = texelFetch(u_hits, h, 0);
            if (hit.z <= 0.0) continue;
            traced += chainAt(hit.xy, hit.w * max(u_maxLod, 1.0)) * (w * hit.z);
            found  += w * hit.z;
        }
    }
    if (found <= 0.0) return;

    const float trust = found / total;
    FragColor.rgb = max(scene.rgb + (weight.rgb * traced / found - added) * trust, vec3(0.0));
}
