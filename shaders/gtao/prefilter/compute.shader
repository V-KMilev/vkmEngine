/**
 * GTAO depth prefilter: linear view depth as a mip chain, so far horizon samples
 * read coarse levels and stay cache-local at any radius.
 *
 * One dispatch a level. Level 0 linearises the scene depth; each later level
 * folds 2x2 of the one before (bound alone, read as level 0), weighted toward
 * the farthest and dropping a texel about a radius nearer, so a coarse level
 * never pulls a distant surface onto a near edge (XeGTAO's depth MIP filter).
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = POST_SLOT_AO_DEPTH) uniform sampler2D u_src;
layout(binding = 0, r32f) uniform writeonly image2D u_dst;
uniform int   u_level;     // 0: u_src is the scene depth; else the level before
uniform float u_radius;    // world-space sample radius; sizes the fold's tolerance

#include "../../depth.glsl"
#include "../../camera.glsl"
#include "../depth_mips.glsl"

float fetch(ivec2 texel) {
    return texelFetch(u_src, clamp(texel, ivec2(0), textureSize(u_src, 0) - 1), 0).r;
}

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(texel, imageSize(u_dst)))) return;

    if (u_level == 0) {
        const float depth = texelFetch(u_src, texel, 0).r;
        imageStore(u_dst, texel, vec4(linearSceneDepth(depth, u_camera.invProjection)));
        return;
    }

    const ivec2 base = texel * 2;
    const float d0 = fetch(base);
    const float d1 = fetch(base + ivec2(1, 0));
    const float d2 = fetch(base + ivec2(0, 1));
    const float d3 = fetch(base + ivec2(1, 1));
    const float farthest = max(max(d0, d1), max(d2, d3));

    // Full weight within KEEP radii of the farthest texel, none past DROP.
    const float KEEP = 0.42;
    const float DROP = 1.09;
    const float scale = 1.0 / ((DROP - KEEP) * u_radius);
    const float w0 = clamp(1.0 - (farthest - d0 - KEEP * u_radius) * scale, 0.0, 1.0);
    const float w1 = clamp(1.0 - (farthest - d1 - KEEP * u_radius) * scale, 0.0, 1.0);
    const float w2 = clamp(1.0 - (farthest - d2 - KEEP * u_radius) * scale, 0.0, 1.0);
    const float w3 = clamp(1.0 - (farthest - d3 - KEEP * u_radius) * scale, 0.0, 1.0);
    imageStore(u_dst, texel, vec4((w0 * d0 + w1 * d1 + w2 * d2 + w3 * d3) / (w0 + w1 + w2 + w3)));
}
