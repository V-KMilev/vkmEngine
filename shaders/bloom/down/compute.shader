/**
 * Bloom downsample - Call of Duty 13-tap filter, one chain level per dispatch.
 *
 * The first downsample (u_karis = 1) caps each 2x2 group, drops a NaN, soft-knee thresholds and
 * Karis-averages them, so one firefly cannot bloom the screen nor a NaN spread down the chain.
 * The source is bound alone and read at its level 0, so one shader serves every level.
 * Compute, because a dispatch costs the driver far less than a framebuffer bind and a draw.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = BLOOM_SLOT_SOURCE) uniform sampler2D u_src;
layout(binding = 0, rgba16f) uniform writeonly image2D u_dst;

uniform int   u_karis;
uniform float u_threshold;
uniform float u_knee;

// The brightest a group of the scene enters the chain at.
const float MAX_RADIANCE = 64.0;

#include "../../downsample.glsl"

// Soft-knee prefilter: the bright portion of c; knee = threshold = 0 returns c.
vec3 softKnee(vec3 c) {
    float brightness = max(c.r, max(c.g, c.b));
    float knee = max(u_knee, 1e-4);
    vec3  curve = vec3(u_threshold - knee, 2.0 * knee, 0.25 / knee);
    float soft  = max(brightness - curve.x, 0.0);
    soft = clamp((soft * soft) * curve.z, 0.0, soft * curve.y * 0.5);
    float contribution = max(soft, brightness - u_threshold) / max(brightness, 1e-4);
    return c * contribution;
}

void main() {
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size  = imageSize(u_dst);
    if (any(greaterThanEqual(texel, size))) return;

    DownsampleGroups groups = downsampleGroups(u_src, (vec2(texel) + 0.5) / vec2(size));

    vec3 result;
    if (u_karis == 1) {
        for (int n = 0; n < 5; ++n) {
            vec3 g = groups.g[n];
            if (any(isnan(g))) g = vec3(0.0);
            groups.g[n] = softKnee(min(g, vec3(MAX_RADIANCE)));
        }
        result = karisAverage(groups);
    } else {
        result = downsample13(groups);
    }

    imageStore(u_dst, texel, vec4(max(result, vec3(0.0)), 1.0));
}
