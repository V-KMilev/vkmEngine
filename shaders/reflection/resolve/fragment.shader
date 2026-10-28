/**
 * Reflection resolve: the traced color in place of the environment's. The
 * forward pass (forward/pbr/fragment.shader) added weight x environment and
 * wrote that product beside it; this adds (weight x traced - product) x trust.
 *
 * A glossy pixel also reads its neighbours' rays, further apart the rougher it
 * is, each only as far as it lies on the same surface - one ray per pixel made
 * a lobe, and the edge of found rays a fade, with no temporal filter.
 *
 * A pixel lying on a planar reflector's rectangle takes its picture instead
 * (GLPlanarReflectionPass): the scene mirrored in the plane, at the same place on
 * the screen, bent by how far the pixel's normal leans off the plane's, and read
 * blurred as far as its roughness spreads.
 */

out vec4 FragColor;

layout(binding = POST_SLOT_SCENE_COLOR)   uniform sampler2D u_sceneColor;
layout(binding = POST_SLOT_SCENE_DEPTH)   uniform sampler2D u_sceneDepth;
layout(binding = REFLECT_SLOT_WEIGHT)     uniform sampler2D u_weight;
layout(binding = REFLECT_SLOT_ENV)        uniform sampler2D u_env;
layout(binding = REFLECT_SLOT_TRACE)      uniform sampler2D u_hits;
layout(binding = REFLECT_SLOT_CHAIN)      uniform sampler2D u_chain;
layout(binding = REFLECT_SLOT_PLANAR_0)   uniform sampler2D u_planar0;
layout(binding = REFLECT_SLOT_PLANAR_1)   uniform sampler2D u_planar1;

uniform float u_maxRoughness;
uniform float u_maxLod;  // the color chain's last level; the trace stored hit levels over it
uniform int   u_traceStep;  // pixels a hit stands for on each axis: 1, or 2 at half resolution
uniform int   u_traced;     // 1 when the trace ran; 0 leaves all but the planar reflectors as they are

// Each planar reflector's picture's last chain level, and the part of it drawn (x0, y0, x1, y1 in
// [0, 1], black round it); where they lie is reflection.glsl's.
uniform float u_planarLod[MAX_PLANAR_REFLECTORS];
uniform vec4  u_planarRect[MAX_PLANAR_REFLECTORS];

#include "../reflection.glsl"

// Planar reflector @p i's picture at @p uv, at chain level @p lod: held half a texel of the coarser
// level read inside the part drawn, so a blurred read takes in none of the black round it.
vec3 planarPicture(int i, vec2 uv, float lod) {
    const int  level = int(ceil(lod));
    const vec2 texel = 1.0 / vec2(i == 0 ? textureSize(u_planar0, level) : textureSize(u_planar1, level));
    const vec4 rect  = u_planarRect[i];
    const vec2 inset = min(0.5 * texel, 0.5 * (rect.zw - rect.xy));
    const vec2 at    = clamp(uv, rect.xy + inset, rect.zw - inset);
    return i == 0 ? textureLod(u_planar0, at, lod).rgb : textureLod(u_planar1, at, lod).rgb;
}

void main() {
    const ivec2 texel = ivec2(gl_FragCoord.xy);
    const vec4  scene = texelFetch(u_sceneColor, texel, 0);
    FragColor = scene;

    const vec4  weight    = texelFetch(u_weight, texel, 0);
    const float roughness = weight.a;
    if (!tracesReflection(weight, u_maxRoughness)) return;
    if (!sceneHasNormal(texel)) return;

    const vec2  size   = vec2(textureSize(u_sceneDepth, 0));
    const ivec2 last   = textureSize(u_hits, 0) - 1;
    const vec3  p      = sceneViewPosAt(texel, size);
    const vec3  n      = sceneNormalAt(texel);

    // Faded toward the roughness limit, not cut, so a roughness map draws no contour.
    const float glossy = 1.0 - smoothstep(0.75 * u_maxRoughness, u_maxRoughness, roughness);
    const vec3  added   = texelFetch(u_env, texel, 0).rgb;

    const int planar = planarAt(p, n);
    if (planar >= 0) {
        const vec2  uv  = (vec2(texel) + 0.5) / size + (n.xy - u_planarPlane[planar].xy) * PLANAR_BEND;
        const float lod = clamp(log2(1.0 + roughness * roughness * 256.0), 0.0, u_planarLod[planar]);
        const vec3  seen = planarPicture(planar, uv, lod);
        FragColor.rgb = max(scene.rgb + (weight.rgb * seen - added) * glossy, vec3(0.0));
        return;
    }
    if (u_traced == 0) return;

    const float spread = roughness / u_maxRoughness;
    // Traced at half resolution, a mirror gathers too: the hits about it, from the pixels on its
    // own surface, are its upsampling.
    const int   reach  = spread < 0.15 && u_traceStep == 1 ? 0 : 1;
    const int   apart  = 1 + int(spread * 2.5);
    const ivec2 own    = texel / u_traceStep;

    vec3  traced = vec3(0.0);
    float found  = 0.0;
    float total  = 0.0;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const ivec2 h = clamp(own + ivec2(dx, dy) * apart, ivec2(0), last);
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
            traced += textureLod(u_chain, hit.xy, hit.w * max(u_maxLod, 1.0)).rgb * (w * hit.z);
            found  += w * hit.z;
        }
    }
    if (found <= 0.0) return;

    const float trust = found / total * glossy;
    FragColor.rgb = max(scene.rgb + (weight.rgb * traced / found - added) * trust, vec3(0.0));
}
