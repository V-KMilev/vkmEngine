/**
 * Reflection resolve: the traced colour in place of the environment's. The
 * forward pass (forward/pbr/fragment.shader) added weight x environment and
 * wrote that product beside it; this adds (weight x traced - product) x trust.
 *
 * A glossy pixel also reads its neighbours' rays, further apart the rougher it
 * is, each only as far as it lies on the same surface - one ray per pixel made
 * a lobe, and the edge of found rays a fade, with no temporal filter.
 */

#include "../../depth.glsl"
#include "../../camera.glsl"
#include "../../normal_codec.glsl"

out vec4 FragColor;

layout(binding = POST_SLOT_SCENE_COLOR)   uniform sampler2D u_scene;
layout(binding = POST_SLOT_SCENE_DEPTH)   uniform sampler2D u_sceneDepth;
layout(binding = POST_SLOT_SCENE_GBUFFER) uniform sampler2D u_sceneGBuffer;
layout(binding = REFLECT_SLOT_WEIGHT)     uniform sampler2D u_weight;
layout(binding = REFLECT_SLOT_ENV)        uniform sampler2D u_env;
layout(binding = REFLECT_SLOT_TRACE)      uniform sampler2D u_hits;
layout(binding = REFLECT_SLOT_CHAIN)      uniform sampler2D u_chain;

uniform float u_maxRoughness;
uniform float u_maxLod;  // the colour chain's last level; the trace stored hit levels over it

#include "../reflection.glsl"

void main() {
    const ivec2 texel = ivec2(gl_FragCoord.xy);
    const vec4  scene = texelFetch(u_scene, texel, 0);
    FragColor = scene;

    const vec4  weight    = texelFetch(u_weight, texel, 0);
    const float roughness = weight.a;
    if (!tracesReflection(weight, u_maxRoughness)) return;
    if (!sceneHasNormal(texel)) return;

    const vec2  size   = vec2(textureSize(u_hits, 0));
    const ivec2 last   = ivec2(size) - 1;
    const vec3  p      = sceneViewPosAt(texel, size);
    const vec3  n      = sceneNormalAt(texel);
    const float spread = roughness / u_maxRoughness;
    const int   reach  = spread < 0.15 ? 0 : 1;
    const int   apart  = 1 + int(spread * 2.5);

    vec3  traced = vec3(0.0);
    float found  = 0.0;
    float total  = 0.0;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const ivec2 q = clamp(texel + ivec2(dx, dy) * apart, ivec2(0), last);
            float w = exp(-0.5 * float(dx * dx + dy * dy));
            if (q != texel) {
                // Off this pixel's plane, or turned from it: another surface.
                if (!sceneHasNormal(q)) continue;
                const float off = abs(dot(n, sceneViewPosAt(q, size) - p));
                w *= 1.0 - smoothstep(0.005, 0.02, off / -p.z);
                w *= pow(max(dot(sceneNormalAt(q), n), 0.0), 8.0);
            }
            total += w;

            const vec4 hit = texelFetch(u_hits, q, 0);
            if (hit.z <= 0.0) continue;
            traced += textureLod(u_chain, hit.xy, hit.w * max(u_maxLod, 1.0)).rgb * (w * hit.z);
            found  += w * hit.z;
        }
    }
    if (found <= 0.0) return;

    // Faded toward the roughness limit, not cut, so a roughness map draws no contour.
    const float trust = found / total * (1.0 - smoothstep(0.75 * u_maxRoughness, u_maxRoughness, roughness));
    const vec3  added = texelFetch(u_env, texel, 0).rgb;
    FragColor.rgb = max(scene.rgb + (weight.rgb * traced / found - added) * trust, vec3(0.0));
}
