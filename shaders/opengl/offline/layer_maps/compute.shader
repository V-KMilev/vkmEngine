/*
 * One level of one layer of a layered material's map array (GLLayerMaps): the layer's own map, read
 * at the level of it matching the one written, or a constant where the layer has none.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = 0) uniform sampler2D u_source;
layout(binding = 0, rgba8) uniform writeonly image2DArray u_dst;

uniform int   u_copy;   // LAYER_COPY_*: what the source is, and what of it is written
uniform int   u_layer;  // the array layer written
uniform float u_lod;    // the source level whose texels match the level written
uniform vec4  u_fill;   // LAYER_COPY_FILL's value

#include "../../color.glsl"

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size  = imageSize(u_dst).xy;
    if (any(greaterThanEqual(texel, size))) return;

    vec4 value = u_fill;
    if (u_copy != LAYER_COPY_FILL) {
        const vec4 source = textureLod(u_source, (vec2(texel) + 0.5) / vec2(size), u_lod);
        // The albedo array is sampled through an sRGB view, which an image cannot be written as.
        if (u_copy == LAYER_COPY_ALBEDO) value = vec4(linearToSrgb(source.rgb), 1.0);
        if (u_copy == LAYER_COPY_NORMAL) value = vec4(source.rg, 0.0, 1.0);
        if (u_copy == LAYER_COPY_AO_ROUGHNESS_METALLIC) value = vec4(source.rgb, 1.0);
        // glTF's: roughness in green and metallic in blue, with no occlusion.
        if (u_copy == LAYER_COPY_ROUGHNESS_METALLIC) value = vec4(1.0, source.gb, 1.0);
    }
    imageStore(u_dst, ivec3(texel, u_layer), value);
}
