/**
 * The water's shore, first half of its distances (GLWaterShore): for each texel, the least of a
 * row's squared distances to its texels, each texel's own depth under the water counted
 * SHORE_UNDER times on top. water/shore/columns takes the least down the columns: two passes of
 * a distance transform, exact within u_reach texels (Felzenszwalb and Huttenlocher 2012).
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

// scene/water/shore's under draw: meters under the water, 0 inside; and its over draw: 0 inside,
// else nothing.
layout(binding = WATER_SLOT_SHORE_UNDER) uniform sampler2D u_under;
layout(binding = WATER_SLOT_SHORE_OVER)  uniform sampler2D u_over;
layout(binding = 0, r32f) uniform writeonly image2D u_rows;
uniform float u_texel;  // meters across a texel
uniform int   u_reach;  // texels searched either way

// How far under the water a thing counts for, against across: a post's foot a meter under is
// no waterline, but the post where it breaks the surface is.
const float SHORE_UNDER = 3.0;

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size  = imageSize(u_rows);
    if (texel.x >= size.x || texel.y >= size.y) return;
    float nearest = 1.0e8;
    const int from = max(texel.x - u_reach, 0);
    const int to   = min(texel.x + u_reach, size.x - 1);
    for (int x = from; x <= to; ++x) {
        const ivec2 at     = ivec2(x, texel.y);
        const float under  = SHORE_UNDER * min(texelFetch(u_under, at, 0).r, texelFetch(u_over, at, 0).r);
        const float across = float(x - texel.x) * u_texel;
        nearest = min(nearest, across * across + under * under);
    }
    imageStore(u_rows, texel, vec4(nearest));
}
