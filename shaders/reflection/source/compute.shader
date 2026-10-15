/**
 * Reflection source: the lit scene copied into the base of the resolve's chain (chainEntry).
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = POST_SLOT_SCENE_COLOR) uniform sampler2D u_sceneColor;
layout(binding = 0, rgba16f) uniform writeonly image2D u_dst;

#include "../../downsample.glsl"

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(texel, imageSize(u_dst)))) return;

    imageStore(u_dst, texel, vec4(chainEntry(texelFetch(u_sceneColor, texel, 0).rgb), 1.0));
}
