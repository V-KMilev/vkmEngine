/**
 * A lightmap page's texels spread one texel past the charts (GLLightmapBaker): an uncovered texel
 * takes its nearest covered neighbour's place - one beside it before one at a corner - so the
 * filtering that reads across a chart's edge finds a texel baked for nearly the place it shows,
 * not the page's empty. The nearest, not a mean: between two charts a mean of their places can lie
 * inside the mesh, and bake dark.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = LIGHTMAP_BAKE_SLOT_POSITION) uniform sampler2D u_position;
layout(binding = LIGHTMAP_BAKE_SLOT_NORMAL)   uniform sampler2D u_normal;

layout(binding = 0, rgba32f) uniform writeonly image2D u_positionOut;
layout(binding = 1, rgba16f) uniform writeonly image2D u_normalOut;

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size  = textureSize(u_position, 0);
    if (texel.x >= size.x || texel.y >= size.y) return;

    vec4 position = texelFetch(u_position, texel, 0);
    vec4 normal   = texelFetch(u_normal, texel, 0);
    if (position.w == 0.0) {
        // The sides, then the corners, each in a fixed order, so a bake is the same every time.
        const ivec2 around[8] = ivec2[8](
            ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1),
            ivec2(1, 1), ivec2(-1, 1), ivec2(1, -1), ivec2(-1, -1)
        );
        for (int i = 0; i < 8; ++i) {
            const ivec2 at = clamp(texel + around[i], ivec2(0), size - 1);
            const vec4  p  = texelFetch(u_position, at, 0);
            if (p.w == 0.0) continue;
            position = vec4(p.xyz, 1.0);
            normal   = vec4(texelFetch(u_normal, at, 0).xyz, 1.0);
            break;
        }
    }
    imageStore(u_positionOut, texel, position);
    imageStore(u_normalOut, texel, normal);
}
