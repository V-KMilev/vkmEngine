/**
 * Reflection source: the lit scene copied into the base of the resolve's chain,
 * capped so one glint cannot smear every level below, and NaN-free.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = POST_SLOT_SCENE_COLOR) uniform sampler2D u_scene;
layout(binding = 0, rgba16f) uniform writeonly image2D u_dst;

const float MAX_RADIANCE = 64.0;

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(texel, imageSize(u_dst)))) return;

    vec3 color = texelFetch(u_scene, texel, 0).rgb;
    if (any(isnan(color))) color = vec3(0.0);
    imageStore(u_dst, texel, vec4(min(color, vec3(MAX_RADIANCE)), 1.0));
}
