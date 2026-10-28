/**
 * The ocean's shore, first half of its distances (GLOceanFields): for each texel, the least of a
 * row's squared distances to its texels, each texel's own depth under the water counted
 * SHORE_UNDER times on top. ocean/shore_columns takes the least down the columns: two passes of
 * a distance transform, exact within u_reach texels (Felzenszwalb and Huttenlocher 2012).
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = 0) uniform sampler2D u_under;  // ocean/shore's under draw: metres under the water, 0 inside
layout(binding = 1) uniform sampler2D u_over;  // and its over draw: 0 inside, else nothing
layout(binding = 0, r32f) uniform writeonly image2D u_rows;
uniform float u_texel;  // metres across a texel
uniform int   u_reach;  // texels searched either way

// How far under the water a thing counts for, against across: a post's foot a metre under is
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
