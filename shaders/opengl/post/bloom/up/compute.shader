/**
 * Bloom upsample - 3x3 tent filter, one chain level per dispatch.
 *
 * Adds the tent-filtered smaller level into the next larger one. u_filterRadius is a fraction of
 * the frame's width and reaches the same distance vertically, so the glow is round at any aspect.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = BLOOM_SLOT_SOURCE) uniform sampler2D u_src;
layout(binding = 0, rgba16f) uniform image2D u_dst;

uniform float u_filterRadius;

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size  = imageSize(u_dst);
    if (any(greaterThanEqual(texel, size))) return;

    const vec2  uv = (vec2(texel) + 0.5) / vec2(size);
    const float x  = u_filterRadius;
    const float y  = u_filterRadius * float(size.x) / float(size.y);

    vec3 a = textureLod(u_src, uv + vec2(-x,  y), 0.0).rgb;
    vec3 b = textureLod(u_src, uv + vec2( 0,  y), 0.0).rgb;
    vec3 c = textureLod(u_src, uv + vec2( x,  y), 0.0).rgb;

    vec3 d = textureLod(u_src, uv + vec2(-x,  0), 0.0).rgb;
    vec3 e = textureLod(u_src, uv,                0.0).rgb;
    vec3 f = textureLod(u_src, uv + vec2( x,  0), 0.0).rgb;

    vec3 g = textureLod(u_src, uv + vec2(-x, -y), 0.0).rgb;
    vec3 h = textureLod(u_src, uv + vec2( 0, -y), 0.0).rgb;
    vec3 i = textureLod(u_src, uv + vec2( x, -y), 0.0).rgb;

    vec3 result = e * 4.0;
    result += (b + d + f + h) * 2.0;
    result += (a + c + g + i);
    result *= (1.0 / 16.0);

    imageStore(u_dst, texel, imageLoad(u_dst, texel) + vec4(result, 0.0));
}
