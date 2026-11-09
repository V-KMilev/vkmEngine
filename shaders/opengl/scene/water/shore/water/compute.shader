/**
 * The water's height over its shore (GLWaterShore): above its rest, at each texel of a
 * coarse grid over the shore's square, read by water/shore filtered. The waves are meters long, so
 * a grid of them is what each fragment would find, for one read in place of the sum's inversion.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

#include "../../../../include/waves.glsl"

layout(binding = 0, r16f) uniform writeonly image2D u_water;
uniform vec4  u_area;   // the shore's square (shoreUV)
uniform float u_share;  // of the waves, the water's the shore is captured for

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size  = imageSize(u_water);
    if (texel.x >= size.x || texel.y >= size.y) return;
    const vec2 uv = (vec2(texel) + 0.5) / vec2(size);
    const vec2 at = shorePoint(uv, u_area);
    imageStore(u_water, texel, vec4(wavesHeightAt(at, u_share)));
}
