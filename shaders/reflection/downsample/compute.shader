/**
 * Reflection chain downsample: each level from the one before with the 13-tap
 * filter, so glossy reflections blur like a lobe, not in blocks. The first level
 * Karis-weights, so one glint cannot bleed into every level below.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = REFLECT_SLOT_CHAIN) uniform sampler2D u_src;  // the level before, bound alone
layout(binding = 0, rgba16f) uniform writeonly image2D u_dst;
uniform int u_first;

#include "../../downsample.glsl"

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size  = imageSize(u_dst);
    if (any(greaterThanEqual(texel, size))) return;

    const DownsampleGroups groups = downsampleGroups(u_src, (vec2(texel) + 0.5) / vec2(size));
    imageStore(u_dst, texel, vec4(u_first == 1 ? karisAverage(groups) : downsample13(groups), 1.0));
}
