/**
 * One level of a ripple field, from the level above (GLWaveFields): each texel the mean of the
 * four it covers. A plain mean is the filter LEAN mapping wants - of
 * the slopes, the squeeze and the slope squared alike - so a far pixel's level keeps its variance.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = WATER_SLOT_RIPPLES_ABOVE) uniform sampler2DArray u_fields;
layout(binding = 0, rgba16f) uniform writeonly image2DArray u_level;
uniform int u_above;  // the level read
uniform int u_field;  // the layer

void main() {
    const ivec3 texel = ivec3(ivec2(gl_GlobalInvocationID.xy), u_field);
    const ivec3 size  = imageSize(u_level);
    if (texel.x >= size.x || texel.y >= size.y) return;
    const ivec3 corner = ivec3(texel.xy * 2, texel.z);
    const vec4  sum    = texelFetch(u_fields, corner, u_above)
                       + texelFetch(u_fields, corner + ivec3(1, 0, 0), u_above)
                       + texelFetch(u_fields, corner + ivec3(0, 1, 0), u_above)
                       + texelFetch(u_fields, corner + ivec3(1, 1, 0), u_above);
    imageStore(u_level, texel, 0.25 * sum);
}
