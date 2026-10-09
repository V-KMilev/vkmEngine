/**
 * The ocean's shore, finished (GLOceanFields): for each texel, the least of a column's
 * ocean/shore_rows plus its own squared distance down the column, rooted: metres to the nearest
 * thing breaking the water, its depth under counted as distance too, out to the reach.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = 0) uniform sampler2D u_rows;
layout(binding = 0, r16f) uniform writeonly image2D u_shore;
uniform float u_texel;  // metres across a texel
uniform int   u_reach;  // texels searched either way

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size  = imageSize(u_shore);
    if (texel.x >= size.x || texel.y >= size.y) return;
    // The square window is exact within its inscribed circle; past it, the reach: so the distance
    // runs on without a step for foam's edge to find.
    const float reach = float(u_reach) * u_texel;
    float nearest = reach * reach;
    const int from = max(texel.y - u_reach, 0);
    const int to   = min(texel.y + u_reach, size.y - 1);
    for (int y = from; y <= to; ++y) {
        const float down = float(y - texel.y) * u_texel;
        nearest = min(nearest, down * down + texelFetch(u_rows, ivec2(texel.x, y), 0).r);
    }
    imageStore(u_shore, texel, vec4(sqrt(nearest)));
}
