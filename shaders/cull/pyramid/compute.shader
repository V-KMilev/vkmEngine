/**
 * GPU occlusion cull - one level of the farthest-depth pyramid (GLOcclusionCull).
 *
 * Level 0 is half the scene, rounded up: a texel is the farthest depth of its two by two pixels,
 * and of every sample of each, so a cull testing against it can only keep too much, never hide what
 * one sample still shows. Every level after is the farthest of the texels below it: two by two, and
 * at the far edge of a level of odd size the third the halving would drop.
 */

layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

#include "../../depth.glsl"  // NEAR_DEPTH, fartherDepth

layout(binding = CULL_SLOT_DEPTH)    uniform sampler2D   u_depth;
layout(binding = CULL_SLOT_DEPTH_MS) uniform sampler2DMS u_depthMS;
layout(binding = CULL_SLOT_PYRAMID)  uniform sampler2D   u_pyramid;  // the level before, at u_level - 1

layout(r32f, binding = 0) uniform writeonly image2D u_out;

uniform int u_level;    // the level written
uniform int u_samples;  // the scene depth's samples; 1 reads u_depth

float farthestOfPixel(ivec2 pixel) {
    if (u_samples <= 1) return texelFetch(u_depth, pixel, 0).r;
    float far = NEAR_DEPTH;
    for (int s = 0; s < u_samples; ++s) {
        far = fartherDepth(far, texelFetch(u_depthMS, pixel, s).r);
    }
    return far;
}

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size  = imageSize(u_out);
    if (any(greaterThanEqual(texel, size))) return;

    float far = NEAR_DEPTH;
    if (u_level == 0) {
        const ivec2 scene = u_samples <= 1 ? textureSize(u_depth, 0) : textureSize(u_depthMS);
        const ivec2 last  = min(texel * 2 + 1, scene - 1);
        for (int y = texel.y * 2; y <= last.y; ++y) {
            for (int x = texel.x * 2; x <= last.x; ++x) {
                far = fartherDepth(far, farthestOfPixel(ivec2(x, y)));
            }
        }
    } else {
        // By GL's halving of level 0's size, as shaders/cull/instances reads the levels.
        const ivec2 below = max(textureSize(u_pyramid, 0) >> (u_level - 1), ivec2(1));
        // The last texel of an odd level below has no parent of its own: the last parent takes it.
        const ivec2 last = texel * 2 + 1 + ivec2(equal(texel, size - 1)) * (below & 1);
        for (int y = texel.y * 2; y <= last.y; ++y) {
            for (int x = texel.x * 2; x <= last.x; ++x) {
                const float depth = texelFetch(u_pyramid, min(ivec2(x, y), below - 1), u_level - 1).r;
                far = fartherDepth(far, depth);
            }
        }
    }
    imageStore(u_out, texel, vec4(far));
}
